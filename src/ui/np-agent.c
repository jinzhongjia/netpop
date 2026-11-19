/* NetworkManager Secret Agent：替代 nm-applet 处理密码请求
 *
 * 流程：
 *   1. 根据请求构造 NpSecretRequest（不支持的类型直接返回 NoSecrets，交给其他 agent）；
 *   2. 若不是“重新输入”，先查钥匙环（兼容 nm-applet 的条目），齐全则直接应答；
 *   3. 允许交互时弹出密码页，否则返回 NoSecrets。
 * SaveSecrets / DeleteSecrets 把 agent 拥有的密钥写入 / 移出钥匙环。
 */
#include "np-actions.h"
#include "np-app.h"
#include "np-keyring.h"
#include "np-pages.h"
#include "np-secrets.h"

#include <nm-secret-agent-old.h>

typedef struct {
    NMSecretAgentOld parent;
} NpAgent;

typedef struct {
    NMSecretAgentOldClass parent_class;
} NpAgentClass;

G_DEFINE_TYPE(NpAgent, np_agent, NM_TYPE_SECRET_AGENT_OLD)

static NMSecretAgentOld *agent;

typedef struct {
    NMSecretAgentOld *agent;
    NMConnection *connection;
    char *path;
    NpSecretRequest *req;
    NMSecretAgentGetSecretsFlags flags;
    NMSecretAgentOldGetSecretsFunc cb;
    gpointer cb_data;
    GCancellable *cancel;
    gboolean prompting;
} Pending;

static GList *pending; /* Pending* */

static void pending_free(Pending *p)
{
    pending = g_list_remove(pending, p);
    g_object_unref(p->connection);
    g_free(p->path);
    np_secret_request_free(p->req);
    g_clear_object(&p->cancel);
    g_free(p);
}

static void reply_error(Pending *p, int code, const char *msg)
{
    GError *e = g_error_new_literal(NM_SECRET_AGENT_ERROR, code, msg);
    p->cb(p->agent, p->connection, NULL, e, p->cb_data);
    g_error_free(e);
}

static void reply_secrets(Pending *p)
{
    GVariant *v = g_variant_ref_sink(np_secret_request_to_variant(p->req));
    p->cb(p->agent, p->connection, v, NULL, p->cb_data);
    g_variant_unref(v);
}

static gboolean auth_done(NpSecretRequest *req, gboolean ok, gpointer data)
{
    Pending *p = data;
    p->req = req; /* 同一个对象，所有权仍在 Pending */
    if (!ok) {
        reply_error(p, NM_SECRET_AGENT_ERROR_USER_CANCELED, "netpop: user canceled");
    } else {
        if (!np_secret_request_is_complete(req)) {
            np_page_auth_error("请填写所有字段");
            return FALSE;
        }
        reply_secrets(p);
        np_notify(NULL, "正在连接 %s…", req->title);
    }
    pending_free(p);
    return TRUE;
}

static void prompt(Pending *p)
{
    if (!(p->flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION)) {
        reply_error(p, NM_SECRET_AGENT_ERROR_NO_SECRETS, "netpop: interaction not allowed");
        pending_free(p);
        return;
    }
    p->prompting = TRUE;
    np_page_auth_ask(p->req, auth_done, p);
}

static void keyring_done(GObject *src, GAsyncResult *res, gpointer data)
{
    Pending *p = data;
    GError *e = NULL;
    GHashTable *values = np_keyring_lookup_finish(res, &e);
    if (g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_error_free(e);
        return; /* 已被 cancel_get_secrets 处理 */
    }
    if (e) {
        g_debug("keyring lookup failed: %s", e->message);
        g_error_free(e);
    }
    np_secret_request_fill(p->req, values);
    if (values) g_hash_table_unref(values);

    if (np_secret_request_is_complete(p->req)) {
        reply_secrets(p);
        pending_free(p);
    } else {
        prompt(p);
    }
}

static void get_secrets(NMSecretAgentOld *self, NMConnection *connection, const char *path,
                        const char *setting_name, const char **hints, NMSecretAgentGetSecretsFlags flags,
                        NMSecretAgentOldGetSecretsFunc cb, gpointer cb_data)
{
    GError *e = NULL;
    gboolean request_new = !!(flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW);
    NpSecretRequest *req = np_secret_request_new(connection, setting_name, (const char *const *)hints, request_new, &e);

    Pending *p = g_new0(Pending, 1);
    p->agent = self;
    p->connection = g_object_ref(connection);
    p->path = g_strdup(path);
    p->req = req;
    p->flags = flags;
    p->cb = cb;
    p->cb_data = cb_data;
    pending = g_list_prepend(pending, p);

    if (!req) {
        /* 交给其他 agent（例如安装了 VPN 插件自带的对话框） */
        reply_error(p, NM_SECRET_AGENT_ERROR_NO_SECRETS, e->message);
        g_error_free(e);
        pending_free(p);
        return;
    }
    if (!request_new && np->config->keyring && np_keyring_available()) {
        p->cancel = g_cancellable_new();
        np_keyring_lookup_async(nm_connection_get_uuid(connection), setting_name, p->cancel, keyring_done, p);
    } else {
        prompt(p);
    }
}

static void cancel_get_secrets(NMSecretAgentOld *self, const char *path, const char *setting_name)
{
    for (GList *l = pending; l; l = l->next) {
        Pending *p = l->data;
        if (g_strcmp0(p->path, path) || g_strcmp0(p->req ? p->req->setting_name : NULL, setting_name)) continue;
        /* NM 要求即使取消也必须回调一次 */
        if (p->prompting) {
            /* 密码页持有 req：先让页面放手，再统一回调 */
            p->prompting = FALSE;
            gboolean was_active = np_page_auth_active();
            if (was_active) {
                np_page_auth_cancel(); /* → auth_done(FALSE)：回复 USER_CANCELED 并释放 */
                if (!g_strcmp0(np_nav_current(), NP_PAGE_AUTH)) np_nav_back();
                return;
            }
        }
        if (p->cancel) g_cancellable_cancel(p->cancel);
        reply_error(p, NM_SECRET_AGENT_ERROR_AGENT_CANCELED, "netpop: canceled by NetworkManager");
        pending_free(p);
        return;
    }
}

static void save_secrets(NMSecretAgentOld *self, NMConnection *c, const char *path,
                         NMSecretAgentOldSaveSecretsFunc cb, gpointer data)
{
    if (np->config->keyring && np_keyring_available()) {
        GPtrArray *items = np_secrets_collect(c, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
        for (guint i = 0; i < items->len; i++) {
            NpSecretItem *it = items->pdata[i];
            np_keyring_store(nm_connection_get_uuid(c), nm_connection_get_id(c), it->setting_name, it->key, it->value);
        }
        g_ptr_array_unref(items);
    }
    cb(self, c, NULL, data);
}

static void delete_secrets(NMSecretAgentOld *self, NMConnection *c, const char *path,
                           NMSecretAgentOldDeleteSecretsFunc cb, gpointer data)
{
    if (np->config->keyring && np_keyring_available()) np_keyring_clear(nm_connection_get_uuid(c));
    cb(self, c, NULL, data);
}

static void np_agent_init(NpAgent *self) {}

static void np_agent_class_init(NpAgentClass *k)
{
    NMSecretAgentOldClass *c = NM_SECRET_AGENT_OLD_CLASS(k);
    c->get_secrets = get_secrets;
    c->cancel_get_secrets = cancel_get_secrets;
    c->save_secrets = save_secrets;
    c->delete_secrets = delete_secrets;
}

static void agent_ready(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    GObject *o = g_async_initable_new_finish(G_ASYNC_INITABLE(src), res, &e);
    if (!o) {
        g_warning("Secret Agent 注册失败: %s", e->message);
        g_error_free(e);
        return;
    }
    agent = NM_SECRET_AGENT_OLD(o);
}

void np_agent_start(void)
{
    if (agent) return;
    g_async_initable_new_async(np_agent_get_type(), G_PRIORITY_DEFAULT, NULL, agent_ready, NULL,
                               NM_SECRET_AGENT_OLD_IDENTIFIER, "dev.netpop.agent",
                               NM_SECRET_AGENT_OLD_CAPABILITIES, NM_SECRET_AGENT_CAPABILITY_VPN_HINTS,
                               NM_SECRET_AGENT_OLD_AUTO_REGISTER, TRUE, NULL);
}

void np_agent_stop(void)
{
    while (pending) {
        Pending *p = pending->data;
        if (p->cancel) g_cancellable_cancel(p->cancel);
        reply_error(p, NM_SECRET_AGENT_ERROR_AGENT_CANCELED, "netpop: shutting down");
        pending_free(p);
    }
    if (agent) {
        nm_secret_agent_old_destroy(agent);
        g_clear_object(&agent);
    }
}
