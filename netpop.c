/*
 * netpop — 轻量的 NetworkManager Wi-Fi 弹出面板（GTK4 + libnm）
 *
 *   netpop            切换显示/隐藏（首次运行会常驻）
 *   netpop --show / --hide / --daemon / --quit
 *
 * 全部操作直接走 libnm（D-Bus），不调用 nmcli / shell。
 * 进程常驻并作为 Secret Agent 处理 Wi-Fi 密码请求。
 */
#include <gtk/gtk.h>
#include <NetworkManager.h>
#include <nm-secret-agent-old.h>
#include <string.h>
#include <stdarg.h>
#include <sys/socket.h>
#ifdef HAVE_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif

#define APP_ID "dev.netpop.Netpop"

/* Nerd Font (Material Design) glyphs */
#define G_W4      "\U000F0928"
#define G_W3      "\U000F0925"
#define G_W2      "\U000F0922"
#define G_W1      "\U000F091F"
#define G_W0      "\U000F092F"
#define G_WOFF    "\U000F05AA"
#define GLY_LOCK    "\U000F033E"
#define G_CHEV    "\U000F0142"
#define G_BACK    "\U000F0141"
#define G_REFRESH "\U000F0450"
#define G_ETH     "\U000F0200"
#define G_VPN     "\U000F0582"
#define G_COG     "\U000F0493"

/* ───────────────────────── 状态 ───────────────────────── */

typedef enum { AUTH_NEW, AUTH_AGENT } AuthKind;

typedef struct {
    AuthKind kind;
    char *name;
    /* AUTH_NEW */
    GBytes *ssid;
    NMDeviceWifi *dev;
    char *ap_path;
    const char *key_mgmt;
    /* AUTH_AGENT */
    NMSecretAgentOld *agent;
    NMConnection *connection;
    char *conn_path;
    char *setting_name;
    NMSecretAgentOldGetSecretsFunc cb;
    gpointer cb_data;
} Auth;

typedef struct {
    char *ssid_str;
    GBytes *ssid;
    NMDeviceWifi *dev;
    NMAccessPoint *ap;
    guint8 strength;
    guint32 freq;
    gboolean secure, enterprise, wep, sae, psk, active, activating;
    NMRemoteConnection *conn; /* 已保存 / 正在使用的连接 */
} Net;

static struct {
    GtkApplication *app;
    NMClient *client;
    NMSecretAgentOld *agent;
    gboolean layer;

    GtkWindow *win;
    GtkStack *stack;
    GtkCssProvider *css;
    char *css_cache;

    /* main */
    GtkSwitch *wifi_switch;
    GtkWidget *scan_btn, *scan_glyph, *scan_spin;
    GtkScrolledWindow *scroller;
    GtkListBox *wifi_list, *other_list;
    GtkLabel *placeholder;
    GtkWidget *other_section;
    gboolean updating;
    guint rebuild_id, scan_anim_id;
    gboolean dirty;
    double restore_scroll;

    /* toast */
    GtkRevealer *toast_rev;
    GtkLabel *toast;
    guint toast_id;

    /* auth */
    Auth *auth;
    GtkLabel *auth_title, *auth_msg;
    GtkWidget *auth_entry;

    /* details */
    NMRemoteConnection *detail;
    GtkLabel *det_title, *det_err;
    GtkGrid *det_grid;
    GtkSwitch *det_autoconnect;
    GtkWidget *det_ip_section, *det_ip_fields, *det_psk_row;
    GtkToggleButton *det_ip_auto, *det_ip_manual;
    GtkEntry *det_addr, *det_gw, *det_dns, *det_psk;
    GtkWidget *det_disconnect, *det_forget, *det_save;
    char *psk_orig;
    guint forget_armed;

    gint64 shown_at;
    gboolean was_active;
} A;

static void queue_rebuild(void);
static void go_main(void);
static void popup_show(void);
static void popup_hide(void);
static void show_details(NMRemoteConnection *rc);
static void fill_detail_info(void);

/* ───────────────────────── 小工具 ───────────────────────── */

static GtkWidget *glyph(const char *g, const char *cls)
{
    GtkWidget *l = gtk_label_new(g);
    gtk_widget_add_css_class(l, "glyph");
    if (cls) gtk_widget_add_css_class(l, cls);
    return l;
}

static GtkWidget *label(const char *text, const char *cls, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls) gtk_widget_add_css_class(l, cls);
    return l;
}

static GtkWidget *button(const char *text, const char *cls, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(text);
    if (cls) gtk_widget_add_css_class(b, cls);
    if (cb) g_signal_connect(b, "clicked", cb, data);
    return b;
}

static GtkWidget *glyph_button(const char *g, const char *tip, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), glyph(g, NULL));
    gtk_widget_add_css_class(b, "icon");
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    if (tip) gtk_widget_set_tooltip_text(b, tip);
    if (cb) g_signal_connect(b, "clicked", cb, data);
    return b;
}

static char *ssid_to_str(GBytes *ssid)
{
    if (!ssid) return g_strdup("");
    gsize len;
    const guint8 *d = g_bytes_get_data(ssid, &len);
    return nm_utils_ssid_to_utf8(d, len);
}

static GBytes *conn_ssid(NMConnection *c)
{
    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    return sw ? nm_setting_wireless_get_ssid(sw) : NULL;
}

static const char *strength_glyph(guint8 s)
{
    return s >= 75 ? G_W4 : s >= 50 ? G_W3 : s >= 25 ? G_W2 : s > 0 ? G_W1 : G_W0;
}

static const char *band(guint32 f)
{
    return f > 5925 ? "6 GHz" : f > 4900 ? "5 GHz" : "2.4 GHz";
}

static void classify(NMAccessPoint *ap, Net *n)
{
    guint32 f = nm_access_point_get_flags(ap);
    guint32 w = nm_access_point_get_wpa_flags(ap), r = nm_access_point_get_rsn_flags(ap);
    n->enterprise = !!((w | r) & (NM_802_11_AP_SEC_KEY_MGMT_802_1X | NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192));
    n->psk = !!((w | r) & NM_802_11_AP_SEC_KEY_MGMT_PSK);
    n->sae = !!(r & NM_802_11_AP_SEC_KEY_MGMT_SAE);
    n->wep = (f & NM_802_11_AP_FLAGS_PRIVACY) && !w && !r;
    n->secure = n->enterprise || n->psk || n->sae || n->wep;
}

static const char *sec_label(const Net *n)
{
    if (n->enterprise) return "企业级";
    if (n->sae && n->psk) return "WPA2/3";
    if (n->sae) return "WPA3";
    if (n->psk) return nm_access_point_get_rsn_flags(n->ap) ? "WPA2" : "WPA";
    if (n->wep) return "WEP";
    return "开放";
}

static NMActiveConnection *active_for(NMRemoteConnection *rc)
{
    if (!A.client || !rc) return NULL;
    const GPtrArray *acs = nm_client_get_active_connections(A.client);
    for (guint i = 0; i < acs->len; i++) {
        NMActiveConnection *ac = acs->pdata[i];
        if (nm_active_connection_get_connection(ac) == rc) return ac;
    }
    return NULL;
}

static NMDevice *ac_device(NMActiveConnection *ac)
{
    const GPtrArray *d = ac ? nm_active_connection_get_devices(ac) : NULL;
    return d && d->len ? d->pdata[0] : NULL;
}

static char *ac_ipv4(NMActiveConnection *ac)
{
    NMIPConfig *cfg = ac ? nm_active_connection_get_ip4_config(ac) : NULL;
    GPtrArray *a = cfg ? nm_ip_config_get_addresses(cfg) : NULL;
    if (!a || !a->len) return NULL;
    NMIPAddress *ip = a->pdata[0];
    return g_strdup_printf("%s/%u", nm_ip_address_get_address(ip), nm_ip_address_get_prefix(ip));
}

static GList *wifi_devices(void)
{
    GList *l = NULL;
    const GPtrArray *devs = nm_client_get_devices(A.client);
    for (guint i = 0; i < devs->len; i++) {
        NMDevice *d = devs->pdata[i];
        if (NM_IS_DEVICE_WIFI(d) && nm_device_get_state(d) > NM_DEVICE_STATE_UNMANAGED)
            l = g_list_append(l, d);
    }
    return l;
}

/* ───────────────────────── toast / 通知 ───────────────────────── */

static gboolean toast_hide(gpointer _)
{
    A.toast_id = 0;
    gtk_revealer_set_reveal_child(A.toast_rev, FALSE);
    return G_SOURCE_REMOVE;
}

static void notify(const char *title, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static void notify(const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *body = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    if (A.win && gtk_widget_get_visible(GTK_WIDGET(A.win))) {
        char *t = title ? g_strdup_printf("%s · %s", title, body) : g_strdup(body);
        gtk_label_set_text(A.toast, t);
        gtk_revealer_set_reveal_child(A.toast_rev, TRUE);
        if (A.toast_id) g_source_remove(A.toast_id);
        A.toast_id = g_timeout_add(3000, toast_hide, NULL);
        g_free(t);
    } else if (title) {
        GNotification *n = g_notification_new(title);
        g_notification_set_body(n, body);
        g_application_send_notification(G_APPLICATION(A.app), "netpop", n);
        g_object_unref(n);
    }
    g_free(body);
}

/* ───────────────────────── 激活跟踪 ───────────────────────── */

typedef struct {
    NMActiveConnection *ac;
    char *name;
    char *conn_path;
    gboolean is_new;
} Attempt;

static const char *fail_reason(NMActiveConnection *ac, guint reason)
{
    if (reason == NM_ACTIVE_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED) {
        NMDevice *d = ac_device(ac);
        switch (d ? nm_device_get_state_reason(d) : 0) {
        case NM_DEVICE_STATE_REASON_NO_SECRETS:
        case NM_DEVICE_STATE_REASON_SUPPLICANT_DISCONNECT: return "密码错误";
        case NM_DEVICE_STATE_REASON_SUPPLICANT_TIMEOUT: return "认证超时";
        case NM_DEVICE_STATE_REASON_SSID_NOT_FOUND: return "找不到该网络";
        case NM_DEVICE_STATE_REASON_IP_CONFIG_UNAVAILABLE: return "获取 IP 失败";
        default: return "设备已断开";
        }
    }
    switch (reason) {
    case NM_ACTIVE_CONNECTION_STATE_REASON_NO_SECRETS: return "未提供密码";
    case NM_ACTIVE_CONNECTION_STATE_REASON_LOGIN_FAILED: return "认证失败";
    case NM_ACTIVE_CONNECTION_STATE_REASON_CONNECT_TIMEOUT: return "连接超时";
    case NM_ACTIVE_CONNECTION_STATE_REASON_IP_CONFIG_INVALID: return "IP 配置无效";
    default: return "连接失败";
    }
}

static void attempt_free(Attempt *t)
{
    g_signal_handlers_disconnect_by_data(t->ac, t);
    g_object_unref(t->ac);
    g_free(t->name);
    g_free(t->conn_path);
    g_free(t);
}

static void attempt_state(NMActiveConnection *ac, guint state, guint reason, Attempt *t)
{
    if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
        notify("已连接", "%s", t->name);
        attempt_free(t);
    } else if (state == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED) {
        if (reason != NM_ACTIVE_CONNECTION_STATE_REASON_USER_DISCONNECTED)
            notify("连接失败", "%s：%s", t->name, fail_reason(ac, reason));
        /* 新建的连接从未成功过，就别留下一个密码错误的配置 */
        if (t->is_new && t->conn_path) {
            NMRemoteConnection *rc = nm_client_get_connection_by_path(A.client, t->conn_path);
            if (rc) nm_remote_connection_delete_async(rc, NULL, NULL, NULL);
        }
        attempt_free(t);
    }
}

static void watch_attempt(NMActiveConnection *ac, const char *name, gboolean is_new)
{
    Attempt *t = g_new0(Attempt, 1);
    t->ac = g_object_ref(ac);
    t->name = g_strdup(name);
    t->is_new = is_new;
    NMRemoteConnection *rc = nm_active_connection_get_connection(ac);
    if (rc) t->conn_path = g_strdup(nm_object_get_path(NM_OBJECT(rc)));
    g_signal_connect(ac, "state-changed", G_CALLBACK(attempt_state), t);
    NMActiveConnectionState s = nm_active_connection_get_state(ac);
    if (s == NM_ACTIVE_CONNECTION_STATE_ACTIVATED || s == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
        attempt_state(ac, s, nm_active_connection_get_state_reason(ac), t);
}

static void activate_done(GObject *src, GAsyncResult *res, gpointer data)
{
    char *name = data;
    GError *e = NULL;
    NMActiveConnection *ac = nm_client_activate_connection_finish(NM_CLIENT(src), res, &e);
    if (e) {
        notify("连接失败", "%s：%s", name, e->message);
        g_error_free(e);
    } else {
        watch_attempt(ac, name, FALSE);
        g_object_unref(ac);
    }
    g_free(name);
    queue_rebuild();
}

static void add_activate_done(GObject *src, GAsyncResult *res, gpointer data)
{
    char *name = data;
    GError *e = NULL;
    NMActiveConnection *ac = nm_client_add_and_activate_connection_finish(NM_CLIENT(src), res, &e);
    if (e) {
        notify("连接失败", "%s：%s", name, e->message);
        g_error_free(e);
    } else {
        watch_attempt(ac, name, TRUE);
        g_object_unref(ac);
    }
    g_free(name);
    queue_rebuild();
}

static void activate(NMRemoteConnection *rc, NMDevice *dev, const char *specific)
{
    const char *name = nm_connection_get_id(NM_CONNECTION(rc));
    nm_client_activate_connection_async(A.client, NM_CONNECTION(rc), dev, specific, NULL,
                                        activate_done, g_strdup(name));
    notify(NULL, "正在连接 %s…", name);
}

static void deactivate_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    if (!nm_client_deactivate_connection_finish(NM_CLIENT(src), res, &e)) {
        notify("断开失败", "%s", e->message);
        g_error_free(e);
    }
}

static void deactivate(NMRemoteConnection *rc)
{
    NMActiveConnection *ac = active_for(rc);
    if (ac) nm_client_deactivate_connection_async(A.client, ac, NULL, deactivate_done, NULL);
}

static void connect_new(GBytes *ssid, const char *name, NMDeviceWifi *dev, const char *ap_path,
                        const char *key_mgmt, const char *psk)
{
    NMConnection *partial = NULL;
    if (psk) {
        partial = nm_simple_connection_new();
        NMSetting *sw = nm_setting_wireless_new();
        g_object_set(sw, NM_SETTING_WIRELESS_SSID, ssid, NULL);
        nm_connection_add_setting(partial, sw);
        NMSetting *ws = nm_setting_wireless_security_new();
        g_object_set(ws,
                     NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, key_mgmt,
                     NM_SETTING_WIRELESS_SECURITY_PSK, psk,
                     /* 由 NM 自己保存密码，不依赖任何 agent/keyring */
                     NM_SETTING_WIRELESS_SECURITY_PSK_FLAGS, NM_SETTING_SECRET_FLAG_NONE,
                     NULL);
        nm_connection_add_setting(partial, ws);
    }
    nm_client_add_and_activate_connection_async(A.client, partial, NM_DEVICE(dev), ap_path, NULL,
                                                add_activate_done, g_strdup(name));
    if (partial) g_object_unref(partial);
    notify(NULL, "正在连接 %s…", name);
}

static const char *psk_invalid(const char *key_mgmt, const char *pw)
{
    size_t n = strlen(pw);
    if (n == 0) return "请输入密码";
    if (g_strcmp0(key_mgmt, "wpa-psk") == 0 && (n < 8 || n > 64))
        return "WPA 密码长度应为 8–63 位";
    return NULL;
}

/* ───────────────────────── 密码页 / Secret Agent ───────────────────────── */

static void auth_free(Auth *a)
{
    if (!a) return;
    g_free(a->name);
    if (a->ssid) g_bytes_unref(a->ssid);
    g_clear_object(&a->dev);
    g_free(a->ap_path);
    g_clear_object(&a->connection);
    g_free(a->conn_path);
    g_free(a->setting_name);
    g_free(a);
}

static void auth_reply_error(Auth *a, int code, const char *msg)
{
    GError *e = g_error_new_literal(NM_SECRET_AGENT_ERROR, code, msg);
    a->cb(a->agent, a->connection, NULL, e, a->cb_data);
    g_error_free(e);
}

/* 取消当前挂起的密码请求（不切页面） */
static void auth_cancel(int code)
{
    Auth *a = A.auth;
    if (!a) return;
    A.auth = NULL;
    if (a->kind == AUTH_AGENT) auth_reply_error(a, code, "netpop: canceled");
    auth_free(a);
}

static void show_auth(Auth *a, const char *msg, gboolean is_error)
{
    auth_cancel(NM_SECRET_AGENT_ERROR_USER_CANCELED);
    A.auth = a;
    char *t = g_strdup_printf("连接到 “%s”", a->name);
    gtk_label_set_text(A.auth_title, t);
    g_free(t);
    gtk_label_set_text(A.auth_msg, msg);
    gtk_widget_remove_css_class(GTK_WIDGET(A.auth_msg), "error");
    if (is_error) gtk_widget_add_css_class(GTK_WIDGET(A.auth_msg), "error");
    gtk_editable_set_text(GTK_EDITABLE(A.auth_entry), "");
    gtk_stack_set_visible_child_name(A.stack, "auth");
    if (!gtk_widget_get_visible(GTK_WIDGET(A.win))) popup_show();
    gtk_widget_grab_focus(A.auth_entry);
}

/* 把 nm-applet 留下的“由 agent 保存”的密码迁移为由 NM 系统保存 */
static void migrate_psk(const char *path, const char *psk)
{
    NMRemoteConnection *rc = nm_client_get_connection_by_path(A.client, path);
    if (!rc) return;
    NMConnection *nc = nm_simple_connection_new_clone(NM_CONNECTION(rc));
    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(nc);
    if (ws) {
        g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_PSK, psk,
                     NM_SETTING_WIRELESS_SECURITY_PSK_FLAGS, NM_SETTING_SECRET_FLAG_NONE, NULL);
        nm_remote_connection_update2(rc, nm_connection_to_dbus(nc, NM_CONNECTION_SERIALIZE_ALL),
                                     NM_SETTINGS_UPDATE2_FLAG_TO_DISK, NULL, NULL, NULL, NULL);
    }
    g_object_unref(nc);
}

static void on_auth_submit(GtkWidget *w, gpointer _)
{
    Auth *a = A.auth;
    if (!a) return;
    const char *pw = gtk_editable_get_text(GTK_EDITABLE(A.auth_entry));
    const char *km = a->key_mgmt;
    if (a->kind == AUTH_AGENT) {
        NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(a->connection);
        km = ws ? nm_setting_wireless_security_get_key_mgmt(ws) : NULL;
    }
    const char *bad = psk_invalid(km, pw);
    if (bad) {
        gtk_label_set_text(A.auth_msg, bad);
        gtk_widget_add_css_class(GTK_WIDGET(A.auth_msg), "error");
        return;
    }

    A.auth = NULL;
    if (a->kind == AUTH_NEW) {
        connect_new(a->ssid, a->name, a->dev, a->ap_path, a->key_mgmt, pw);
    } else {
        GVariantBuilder s, b;
        g_variant_builder_init(&s, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&s, "{sv}", NM_SETTING_WIRELESS_SECURITY_PSK, g_variant_new_string(pw));
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_add(&b, "{sa{sv}}", a->setting_name, &s);
        GVariant *v = g_variant_ref_sink(g_variant_builder_end(&b));
        a->cb(a->agent, a->connection, v, NULL, a->cb_data);
        g_variant_unref(v);

        NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(a->connection);
        if (ws && (nm_setting_wireless_security_get_psk_flags(ws) & NM_SETTING_SECRET_FLAG_AGENT_OWNED))
            migrate_psk(a->conn_path, pw);
        notify(NULL, "正在连接 %s…", a->name);
    }
    auth_free(a);
    go_main();
}

/* — NMSecretAgentOld 子类 — */
typedef struct { NMSecretAgentOld parent; } NetpopAgent;
typedef struct { NMSecretAgentOldClass parent_class; } NetpopAgentClass;
G_DEFINE_TYPE(NetpopAgent, netpop_agent, NM_TYPE_SECRET_AGENT_OLD)

static void agent_get_secrets(NMSecretAgentOld *agent, NMConnection *connection, const char *path,
                              const char *setting_name, const char **hints,
                              NMSecretAgentGetSecretsFlags flags,
                              NMSecretAgentOldGetSecretsFunc cb, gpointer cb_data)
{
    Auth *a = g_new0(Auth, 1);
    a->kind = AUTH_AGENT;
    a->agent = agent;
    a->connection = g_object_ref(connection);
    a->conn_path = g_strdup(path);
    a->setting_name = g_strdup(setting_name);
    a->cb = cb;
    a->cb_data = cb_data;

    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(connection);
    const char *km = ws ? nm_setting_wireless_security_get_key_mgmt(ws) : NULL;
    gboolean handled = g_strcmp0(setting_name, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME) == 0 &&
                       (g_strcmp0(km, "wpa-psk") == 0 || g_strcmp0(km, "sae") == 0) &&
                       (flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION);
    if (!handled) {
        /* 交给其他 agent（例如 VPN 的） */
        auth_reply_error(a, NM_SECRET_AGENT_ERROR_NO_SECRETS, "netpop: not handled");
        auth_free(a);
        return;
    }
    GBytes *ssid = conn_ssid(connection);
    a->name = ssid ? ssid_to_str(ssid) : g_strdup(nm_connection_get_id(connection));
    gboolean again = !!(flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW);
    show_auth(a, again ? "密码错误，请重新输入" : "此网络需要密码", again);
}

static void agent_cancel_get_secrets(NMSecretAgentOld *agent, const char *path, const char *setting_name)
{
    Auth *a = A.auth;
    if (a && a->kind == AUTH_AGENT && g_strcmp0(a->conn_path, path) == 0 &&
        g_strcmp0(a->setting_name, setting_name) == 0) {
        auth_cancel(NM_SECRET_AGENT_ERROR_AGENT_CANCELED);
        go_main();
    }
}

static void agent_save_secrets(NMSecretAgentOld *agent, NMConnection *c, const char *path,
                               NMSecretAgentOldSaveSecretsFunc cb, gpointer data)
{
    cb(agent, c, NULL, data);
}

static void agent_delete_secrets(NMSecretAgentOld *agent, NMConnection *c, const char *path,
                                 NMSecretAgentOldDeleteSecretsFunc cb, gpointer data)
{
    cb(agent, c, NULL, data);
}

static void netpop_agent_init(NetpopAgent *self) {}
static void netpop_agent_class_init(NetpopAgentClass *k)
{
    NMSecretAgentOldClass *c = NM_SECRET_AGENT_OLD_CLASS(k);
    c->get_secrets = agent_get_secrets;
    c->cancel_get_secrets = agent_cancel_get_secrets;
    c->save_secrets = agent_save_secrets;
    c->delete_secrets = agent_delete_secrets;
}

static void agent_ready(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    GObject *o = g_async_initable_new_finish(G_ASYNC_INITABLE(src), res, &e);
    if (e) {
        g_warning("secret agent 注册失败: %s", e->message);
        g_error_free(e);
        return;
    }
    A.agent = NM_SECRET_AGENT_OLD(o);
}

/* ───────────────────────── 列表 ───────────────────────── */

static void net_free(Net *n)
{
    g_free(n->ssid_str);
    g_bytes_unref(n->ssid);
    g_clear_object(&n->dev);
    g_clear_object(&n->ap);
    g_clear_object(&n->conn);
    g_free(n);
}

static int net_rank(const Net *n)
{
    return n->active ? 0 : n->activating ? 1 : n->conn ? 2 : 3;
}

static gint net_cmp(gconstpointer pa, gconstpointer pb)
{
    const Net *a = pa, *b = pb;
    int r = net_rank(a) - net_rank(b);
    if (r) return r;
    if (a->strength != b->strength) return (int)b->strength - (int)a->strength;
    return g_utf8_collate(a->ssid_str, b->ssid_str);
}

static void on_details_clicked(GtkButton *b, NMRemoteConnection *rc) { show_details(rc); }

static GtkWidget *make_row(const char *key, const char *g, const char *title, const char *sub,
                           gboolean active, gboolean busy, gboolean locked, NMRemoteConnection *details)
{
    GtkWidget *row = gtk_list_box_row_new();
    g_object_set_data_full(G_OBJECT(row), "key", g_strdup(key), g_free);
    if (active) gtk_widget_add_css_class(row, "active");

    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(h), glyph(g, "sig"));

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
    GtkWidget *t = label(title, "name", 0);
    gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(v), t);
    if (sub && *sub) {
        GtkWidget *s = label(sub, "sub", 0);
        gtk_label_set_ellipsize(GTK_LABEL(s), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(v), s);
    }
    gtk_box_append(GTK_BOX(h), v);

    if (busy) {
        GtkWidget *sp = gtk_spinner_new();
        gtk_spinner_start(GTK_SPINNER(sp));
        gtk_box_append(GTK_BOX(h), sp);
    }
    if (locked) gtk_box_append(GTK_BOX(h), glyph(GLY_LOCK, "lock"));
    if (details) {
        GtkWidget *b = glyph_button(G_CHEV, "详情与设置", NULL, NULL);
        g_signal_connect_data(b, "clicked", G_CALLBACK(on_details_clicked), g_object_ref(details),
                              (GClosureNotify)(void (*)(void))g_object_unref, 0);
        gtk_box_append(GTK_BOX(h), b);
    }
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), h);
    return row;
}

static GtkWidget *make_wifi_row(Net *n)
{
    GString *s = g_string_new(NULL);
    NMActiveConnection *ac = n->active ? active_for(n->conn) : NULL;
    if (n->active) {
        char *ip = ac_ipv4(ac);
        g_string_append(s, "已连接");
        if (ip) g_string_append_printf(s, " · %s", ip);
        g_free(ip);
    } else if (n->activating) {
        g_string_append(s, "正在连接…");
    } else {
        if (n->conn) g_string_append(s, "已保存 · ");
        g_string_append_printf(s, "%s · %s", sec_label(n), band(n->freq));
    }
    GtkWidget *row = make_row(n->ssid_str, strength_glyph(n->strength), n->ssid_str, s->str,
                              n->active, n->activating, n->secure, n->conn);
    g_string_free(s, TRUE);
    g_object_set_data_full(G_OBJECT(row), "net", n, (GDestroyNotify)net_free);
    return row;
}

static void on_wifi_row(GtkListBox *box, GtkListBoxRow *row, gpointer _)
{
    Net *n = g_object_get_data(G_OBJECT(row), "net");
    if (!n || n->activating) return;
    if (n->active) {
        show_details(n->conn);
        return;
    }
    const char *ap_path = nm_object_get_path(NM_OBJECT(n->ap));
    if (n->conn) {
        activate(n->conn, NM_DEVICE(n->dev), ap_path);
        return;
    }
    if (n->enterprise || n->wep) {
        notify(NULL, "%s 加密的网络请在“高级设置”中配置", sec_label(n));
        return;
    }
    if (!n->secure) {
        connect_new(n->ssid, n->ssid_str, n->dev, ap_path, NULL, NULL);
        return;
    }
    Auth *a = g_new0(Auth, 1);
    a->kind = AUTH_NEW;
    a->name = g_strdup(n->ssid_str);
    a->ssid = g_bytes_ref(n->ssid);
    a->dev = g_object_ref(n->dev);
    a->ap_path = g_strdup(ap_path);
    a->key_mgmt = (n->sae && !n->psk) ? "sae" : "wpa-psk";
    show_auth(a, sec_label(n), FALSE);
}

static void on_other_row(GtkListBox *box, GtkListBoxRow *row, gpointer _)
{
    NMRemoteConnection *rc = g_object_get_data(G_OBJECT(row), "conn");
    if (!rc) return;
    NMActiveConnection *ac = active_for(rc);
    if (ac) deactivate(rc);
    else activate(rc, NULL, NULL);
}

/* gtk_list_box_remove_all() 会连 placeholder 一起移除，这里只删行 */
static void clear_rows(GtkListBox *box)
{
    GtkWidget *c = gtk_widget_get_first_child(GTK_WIDGET(box));
    while (c) {
        GtkWidget *next = gtk_widget_get_next_sibling(c);
        if (GTK_IS_LIST_BOX_ROW(c)) gtk_list_box_remove(box, c);
        c = next;
    }
}

static gboolean restore_scroll(gpointer _)
{
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(A.scroller), A.restore_scroll);
    return G_SOURCE_REMOVE;
}

static gboolean do_rebuild(gpointer _)
{
    A.rebuild_id = 0;
    A.dirty = FALSE;
    if (!A.client) return G_SOURCE_REMOVE;

    A.restore_scroll = gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(A.scroller));
    char *focus_key = NULL;
    GtkWidget *fw = gtk_root_get_focus(GTK_ROOT(A.win));
    GtkWidget *frow = fw ? gtk_widget_get_ancestor(fw, GTK_TYPE_LIST_BOX_ROW) : NULL;
    if (frow) focus_key = g_strdup(g_object_get_data(G_OBJECT(frow), "key"));

    clear_rows(A.wifi_list);
    clear_rows(A.other_list);

    GList *wdevs = wifi_devices();
    gboolean en = nm_client_wireless_get_enabled(A.client);
    gboolean hw = nm_client_wireless_hardware_get_enabled(A.client);
    A.updating = TRUE;
    gtk_switch_set_active(A.wifi_switch, en);
    A.updating = FALSE;
    gtk_widget_set_sensitive(GTK_WIDGET(A.wifi_switch), hw && wdevs);
    gtk_widget_set_sensitive(A.scan_btn, en && wdevs);
    gtk_label_set_text(A.placeholder, !wdevs ? "未检测到无线网卡"
                                      : !hw  ? "无线已被硬件开关禁用"
                                      : !en  ? "Wi-Fi 已关闭"
                                             : "正在搜索网络…");

    const GPtrArray *conns = nm_client_get_connections(A.client);
    GHashTable *saved = g_hash_table_new(g_bytes_hash, g_bytes_equal);
    for (guint i = 0; i < conns->len; i++) {
        GBytes *s = conn_ssid(conns->pdata[i]);
        if (s && !g_hash_table_contains(saved, s)) g_hash_table_insert(saved, s, conns->pdata[i]);
    }

    GHashTable *nets = g_hash_table_new(g_bytes_hash, g_bytes_equal);
    for (GList *l = en ? wdevs : NULL; l; l = l->next) {
        NMDeviceWifi *dev = l->data;
        NMAccessPoint *aap = nm_device_wifi_get_active_access_point(dev);
        const GPtrArray *aps = nm_device_wifi_get_access_points(dev);
        for (guint i = 0; i < aps->len; i++) {
            NMAccessPoint *ap = aps->pdata[i];
            GBytes *s = nm_access_point_get_ssid(ap);
            if (!s || !g_bytes_get_size(s) || nm_access_point_get_mode(ap) != NM_802_11_MODE_INFRA)
                continue;
            guint8 st = nm_access_point_get_strength(ap);
            Net *n = g_hash_table_lookup(nets, s);
            if (!n) {
                n = g_new0(Net, 1);
                n->ssid = g_bytes_ref(s);
                n->ssid_str = ssid_to_str(s);
                g_hash_table_insert(nets, n->ssid, n);
            } else if (n->ap == aap || (ap != aap && st <= n->strength)) {
                continue; /* 同名网络只保留当前使用或信号最强的 AP */
            }
            g_set_object(&n->ap, ap);
            g_set_object(&n->dev, dev);
            n->strength = st;
            n->freq = nm_access_point_get_frequency(ap);
            classify(ap, n);
        }
        NMActiveConnection *ac = nm_device_get_active_connection(NM_DEVICE(dev));
        NMRemoteConnection *arc = ac ? nm_active_connection_get_connection(ac) : NULL;
        GBytes *assid = arc ? conn_ssid(NM_CONNECTION(arc)) : NULL;
        Net *n = assid ? g_hash_table_lookup(nets, assid) : NULL;
        if (n) {
            NMActiveConnectionState st = nm_active_connection_get_state(ac);
            n->active = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATED;
            n->activating = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATING;
            g_set_object(&n->conn, arc);
        }
    }

    GList *list = g_hash_table_get_values(nets);
    for (GList *l = list; l; l = l->next) {
        Net *n = l->data;
        if (!n->conn) g_set_object(&n->conn, g_hash_table_lookup(saved, n->ssid));
    }
    list = g_list_sort(list, net_cmp);
    for (GList *l = list; l; l = l->next) gtk_list_box_append(A.wifi_list, make_wifi_row(l->data));
    g_list_free(list);
    g_hash_table_destroy(nets);
    g_hash_table_destroy(saved);
    g_list_free(wdevs);

    /* 有线 / VPN / WireGuard */
    int others = 0;
    for (guint i = 0; i < conns->len; i++) {
        NMRemoteConnection *rc = conns->pdata[i];
        const char *t = nm_connection_get_connection_type(NM_CONNECTION(rc));
        const char *g = NULL;
        if (!g_strcmp0(t, NM_SETTING_WIRED_SETTING_NAME)) g = G_ETH;
        else if (!g_strcmp0(t, NM_SETTING_VPN_SETTING_NAME) || !g_strcmp0(t, NM_SETTING_WIREGUARD_SETTING_NAME)) g = G_VPN;
        if (!g) continue;
        NMActiveConnection *ac = active_for(rc);
        NMActiveConnectionState st = ac ? nm_active_connection_get_state(ac) : NM_ACTIVE_CONNECTION_STATE_UNKNOWN;
        gboolean on = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATED;
        gboolean busy = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATING || st == NM_ACTIVE_CONNECTION_STATE_DEACTIVATING;
        char *ip = on ? ac_ipv4(ac) : NULL;
        char *sub = on ? g_strdup_printf("已连接%s%s", ip ? " · " : "", ip ? ip : "")
                       : g_strdup(busy ? "处理中…" : "未连接 · 点击连接");
        const char *id = nm_connection_get_id(NM_CONNECTION(rc));
        GtkWidget *row = make_row(nm_object_get_path(NM_OBJECT(rc)), g, id, sub, on, busy, FALSE, rc);
        g_object_set_data_full(G_OBJECT(row), "conn", g_object_ref(rc), g_object_unref);
        gtk_list_box_append(A.other_list, row);
        g_free(ip);
        g_free(sub);
        others++;
    }
    gtk_widget_set_visible(A.other_section, others > 0);

    if (focus_key) {
        GtkListBox *boxes[] = { A.wifi_list, A.other_list };
        for (int b = 0; b < 2; b++)
            for (GtkWidget *c = gtk_widget_get_first_child(GTK_WIDGET(boxes[b])); c; c = gtk_widget_get_next_sibling(c))
                if (GTK_IS_LIST_BOX_ROW(c) && !g_strcmp0(g_object_get_data(G_OBJECT(c), "key"), focus_key))
                    gtk_widget_grab_focus(c);
        g_free(focus_key);
    }
    g_idle_add(restore_scroll, NULL);

    if (A.detail) {
        gboolean exists = FALSE;
        for (guint i = 0; i < conns->len && !exists; i++) exists = conns->pdata[i] == (gpointer)A.detail;
        if (!exists) go_main();
        else fill_detail_info();
    }
    return G_SOURCE_REMOVE;
}

static void queue_rebuild(void)
{
    if (!A.win || !gtk_widget_get_visible(GTK_WIDGET(A.win))) {
        A.dirty = TRUE; /* 隐藏时不做任何 UI 工作 */
        return;
    }
    if (!A.rebuild_id) A.rebuild_id = g_timeout_add(80, do_rebuild, NULL);
}

/* ───────────────────────── 扫描 / 开关 ───────────────────────── */

static gboolean scan_anim_stop(gpointer _)
{
    A.scan_anim_id = 0;
    gtk_widget_set_visible(A.scan_spin, FALSE);
    gtk_widget_set_visible(A.scan_glyph, TRUE);
    return G_SOURCE_REMOVE;
}

static void request_scan(gboolean animate, gint64 min_age_ms)
{
    if (!A.client) return;
    GList *devs = wifi_devices();
    gint64 now = nm_utils_get_timestamp_msec();
    for (GList *l = devs; l; l = l->next) {
        gint64 last = nm_device_wifi_get_last_scan(l->data);
        if (last < 0 || now - last >= min_age_ms)
            nm_device_wifi_request_scan_async(l->data, NULL, NULL, NULL);
    }
    g_list_free(devs);
    if (animate) {
        gtk_widget_set_visible(A.scan_glyph, FALSE);
        gtk_widget_set_visible(A.scan_spin, TRUE);
        if (A.scan_anim_id) g_source_remove(A.scan_anim_id);
        A.scan_anim_id = g_timeout_add_seconds(8, scan_anim_stop, NULL);
    }
}

static void on_scan_clicked(GtkButton *b, gpointer _) { request_scan(TRUE, 0); }

static void on_last_scan(void)
{
    if (A.scan_anim_id) {
        g_source_remove(A.scan_anim_id);
        scan_anim_stop(NULL);
    }
    queue_rebuild();
}

static void wifi_set_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    if (!nm_client_dbus_set_property_finish(NM_CLIENT(src), res, &e)) {
        notify("无法切换 Wi-Fi", "%s", e->message);
        g_error_free(e);
        queue_rebuild();
    }
}

static void on_wifi_switch(GObject *sw, GParamSpec *ps, gpointer _)
{
    if (A.updating || !A.client) return;
    nm_client_dbus_set_property(A.client, NM_DBUS_PATH, NM_DBUS_INTERFACE, "WirelessEnabled",
                                g_variant_new_boolean(gtk_switch_get_active(GTK_SWITCH(sw))), -1, NULL,
                                wifi_set_done, NULL);
}

/* ───────────────────────── 详情页 ───────────────────────── */

static void info_row(const char *k, const char *v, int *r)
{
    if (!v || !*v) return;
    GtkWidget *kl = label(k, "key", 0);
    gtk_widget_set_valign(kl, GTK_ALIGN_START);
    GtkWidget *vl = label(v, "val", 0);
    gtk_label_set_selectable(GTK_LABEL(vl), TRUE);
    gtk_label_set_wrap(GTK_LABEL(vl), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(vl), PANGO_WRAP_WORD_CHAR);
    gtk_widget_set_hexpand(vl, TRUE);
    gtk_widget_set_focusable(vl, FALSE);
    gtk_grid_attach(A.det_grid, kl, 0, *r, 1, 1);
    gtk_grid_attach(A.det_grid, vl, 1, *r, 1, 1);
    (*r)++;
}

static void fill_detail_info(void)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(A.det_grid))))
        gtk_grid_remove(A.det_grid, c);
    if (!A.detail) return;

    int r = 0;
    NMActiveConnection *ac = active_for(A.detail);
    NMActiveConnectionState st = ac ? nm_active_connection_get_state(ac) : NM_ACTIVE_CONNECTION_STATE_UNKNOWN;
    gtk_widget_set_visible(A.det_disconnect, ac != NULL);
    info_row("状态", st == NM_ACTIVE_CONNECTION_STATE_ACTIVATED    ? "已连接"
                     : st == NM_ACTIVE_CONNECTION_STATE_ACTIVATING ? "正在连接…"
                     : ac                                          ? "正在断开…"
                                                                   : "未连接", &r);
    if (!ac) return;

    NMDevice *dev = ac_device(ac);
    if (NM_IS_DEVICE_WIFI(dev)) {
        NMAccessPoint *ap = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(dev));
        if (ap) {
            Net n = { .ap = ap };
            classify(ap, &n);
            guint32 f = nm_access_point_get_frequency(ap);
            char *sig = g_strdup_printf("%u%%", nm_access_point_get_strength(ap));
            char *fr = g_strdup_printf("%s · 信道 %u", band(f), nm_utils_wifi_freq_to_channel(f));
            char *rate = g_strdup_printf("%u Mb/s", nm_device_wifi_get_bitrate(NM_DEVICE_WIFI(dev)) / 1000);
            info_row("信号", sig, &r);
            info_row("频段", fr, &r);
            info_row("速率", rate, &r);
            info_row("安全", sec_label(&n), &r);
            info_row("BSSID", nm_access_point_get_bssid(ap), &r);
            g_free(sig); g_free(fr); g_free(rate);
        }
    }

    NMIPConfig *ip4 = nm_active_connection_get_ip4_config(ac);
    if (ip4) {
        GString *s = g_string_new(NULL);
        GPtrArray *addrs = nm_ip_config_get_addresses(ip4);
        for (guint i = 0; i < addrs->len; i++)
            g_string_append_printf(s, "%s%s/%u", i ? "\n" : "", nm_ip_address_get_address(addrs->pdata[i]),
                                   nm_ip_address_get_prefix(addrs->pdata[i]));
        info_row("IPv4", s->str, &r);
        info_row("网关", nm_ip_config_get_gateway(ip4), &r);
        const char *const *ns = nm_ip_config_get_nameservers(ip4);
        char *dns = ns ? g_strjoinv(", ", (char **)ns) : NULL;
        info_row("DNS", dns, &r);
        g_free(dns);
        g_string_free(s, TRUE);
    }
    NMIPConfig *ip6 = nm_active_connection_get_ip6_config(ac);
    if (ip6) {
        GString *s = g_string_new(NULL);
        GPtrArray *addrs = nm_ip_config_get_addresses(ip6);
        for (guint i = 0; i < addrs->len; i++) {
            const char *a = nm_ip_address_get_address(addrs->pdata[i]);
            if (g_str_has_prefix(a, "fe80")) continue;
            g_string_append_printf(s, "%s%s", s->len ? "\n" : "", a);
        }
        info_row("IPv6", s->str, &r);
        g_string_free(s, TRUE);
    }
    if (dev) {
        info_row("接口", nm_device_get_iface(dev), &r);
        info_row("MAC", nm_device_get_hw_address(dev), &r);
    }
}

static void set_det_error(const char *msg)
{
    gtk_label_set_text(A.det_err, msg ? msg : "");
    gtk_widget_set_visible(GTK_WIDGET(A.det_err), msg != NULL);
}

static void on_ip_mode(GtkToggleButton *b, gpointer _)
{
    gtk_widget_set_sensitive(A.det_ip_fields, gtk_toggle_button_get_active(A.det_ip_manual));
}

static void show_details(NMRemoteConnection *rc)
{
    if (!rc) return;
    g_set_object(&A.detail, rc);
    NMConnection *c = NM_CONNECTION(rc);
    gtk_label_set_text(A.det_title, nm_connection_get_id(c));
    set_det_error(NULL);
    gtk_button_set_label(GTK_BUTTON(A.det_forget), "忘记");
    if (A.forget_armed) { g_source_remove(A.forget_armed); A.forget_armed = 0; }
    gtk_widget_set_sensitive(A.det_save, TRUE);

    NMSettingConnection *sc = nm_connection_get_setting_connection(c);
    gtk_switch_set_active(A.det_autoconnect, sc ? nm_setting_connection_get_autoconnect(sc) : TRUE);

    const char *type = nm_connection_get_connection_type(c);
    gboolean ip_editable = !g_strcmp0(type, NM_SETTING_WIRELESS_SETTING_NAME) ||
                           !g_strcmp0(type, NM_SETTING_WIRED_SETTING_NAME);
    gtk_widget_set_visible(A.det_ip_section, ip_editable);

    NMSettingIPConfig *ip4 = nm_connection_get_setting_ip4_config(c);
    gboolean manual = ip4 && !g_strcmp0(nm_setting_ip_config_get_method(ip4), NM_SETTING_IP4_CONFIG_METHOD_MANUAL);
    gtk_toggle_button_set_active(manual ? A.det_ip_manual : A.det_ip_auto, TRUE);
    on_ip_mode(NULL, NULL);

    char *addr = NULL, *dns = NULL;
    if (ip4 && nm_setting_ip_config_get_num_addresses(ip4)) {
        NMIPAddress *a = nm_setting_ip_config_get_address(ip4, 0);
        addr = g_strdup_printf("%s/%u", nm_ip_address_get_address(a), nm_ip_address_get_prefix(a));
    }
    if (ip4 && nm_setting_ip_config_get_num_dns(ip4)) {
        GString *s = g_string_new(NULL);
        for (guint i = 0; i < nm_setting_ip_config_get_num_dns(ip4); i++)
            g_string_append_printf(s, "%s%s", i ? ", " : "", nm_setting_ip_config_get_dns(ip4, i));
        dns = g_string_free(s, FALSE);
    }
    gtk_editable_set_text(GTK_EDITABLE(A.det_addr), addr ? addr : "");
    gtk_editable_set_text(GTK_EDITABLE(A.det_gw), ip4 && nm_setting_ip_config_get_gateway(ip4) ? nm_setting_ip_config_get_gateway(ip4) : "");
    gtk_editable_set_text(GTK_EDITABLE(A.det_dns), dns ? dns : "");
    g_free(addr);
    g_free(dns);

    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
    const char *km = ws ? nm_setting_wireless_security_get_key_mgmt(ws) : NULL;
    gtk_widget_set_visible(A.det_psk_row, !g_strcmp0(km, "wpa-psk") || !g_strcmp0(km, "sae"));
    gtk_editable_set_text(GTK_EDITABLE(A.det_psk), "");
    gtk_entry_set_visibility(A.det_psk, FALSE);
    g_clear_pointer(&A.psk_orig, g_free);

    fill_detail_info();
    gtk_stack_set_visible_child_name(A.stack, "details");
}

static void got_secrets(GObject *src, GAsyncResult *res, gpointer data)
{
    GError *e = NULL;
    GVariant *v = nm_remote_connection_get_secrets_finish(NM_REMOTE_CONNECTION(src), res, &e);
    if ((gpointer)src != A.detail) goto out;
    if (e) {
        set_det_error(e->message);
        goto out;
    }
    GVariant *s = g_variant_lookup_value(v, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, G_VARIANT_TYPE("a{sv}"));
    const char *psk = NULL;
    if (s) g_variant_lookup(s, NM_SETTING_WIRELESS_SECURITY_PSK, "&s", &psk);
    if (psk) {
        g_free(A.psk_orig);
        A.psk_orig = g_strdup(psk);
        gtk_editable_set_text(GTK_EDITABLE(A.det_psk), psk);
        gtk_entry_set_visibility(A.det_psk, TRUE);
    } else {
        set_det_error("密码未由 NetworkManager 保存（可能存放在 keyring 中）");
    }
    if (s) g_variant_unref(s);
out:
    if (v) g_variant_unref(v);
    if (e) g_error_free(e);
}

static void on_psk_icon(GtkEntry *entry, GtkEntryIconPosition pos, gpointer _)
{
    if (gtk_entry_get_visibility(entry)) {
        gtk_entry_set_visibility(entry, FALSE);
    } else if (!A.psk_orig && !*gtk_editable_get_text(GTK_EDITABLE(entry)) && A.detail) {
        nm_remote_connection_get_secrets_async(A.detail, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL,
                                               got_secrets, NULL);
    } else {
        gtk_entry_set_visibility(entry, TRUE);
    }
}

typedef struct {
    NMRemoteConnection *rc;
    NMConnection *nc;
    gboolean psk_changed, ip_changed;
} SaveCtx;

static void save_ctx_free(SaveCtx *x)
{
    g_object_unref(x->rc);
    g_object_unref(x->nc);
    g_free(x);
}

static void reapply_done(GObject *src, GAsyncResult *res, gpointer data)
{
    SaveCtx *x = data;
    GError *e = NULL;
    if (!nm_device_reapply_finish(NM_DEVICE(src), res, &e)) {
        g_error_free(e);
        activate(x->rc, NM_DEVICE(src), NULL); /* 不能热应用就重连 */
    }
    save_ctx_free(x);
}

static void save_done(GObject *src, GAsyncResult *res, gpointer data)
{
    SaveCtx *x = data;
    GError *e = NULL;
    GVariant *r = nm_remote_connection_update2_finish(NM_REMOTE_CONNECTION(src), res, &e);
    if (r) g_variant_unref(r);
    if (x->rc == A.detail) gtk_widget_set_sensitive(A.det_save, TRUE);
    if (e) {
        if (x->rc == A.detail) set_det_error(e->message);
        g_error_free(e);
        save_ctx_free(x);
        return;
    }
    notify(NULL, "已保存 %s", nm_connection_get_id(x->nc));
    NMActiveConnection *ac = active_for(x->rc);
    NMDevice *dev = ac_device(ac);
    if (ac && x->psk_changed) {
        activate(x->rc, dev, NULL);
    } else if (ac && x->ip_changed && dev) {
        nm_device_reapply_async(dev, x->nc, 0, 0, NULL, reapply_done, x);
        return;
    }
    save_ctx_free(x);
    if (x->rc == A.detail) go_main();
}

static void on_det_save(GtkButton *b, gpointer _)
{
    NMRemoteConnection *rc = A.detail;
    if (!rc) return;
    NMConnection *nc = nm_simple_connection_new_clone(NM_CONNECTION(rc));
    GError *e = NULL;
    const char *err = NULL;
    char **parts = NULL, **dns = NULL;

    NMSettingConnection *sc = nm_connection_get_setting_connection(nc);
    g_object_set(sc, NM_SETTING_CONNECTION_AUTOCONNECT, gtk_switch_get_active(A.det_autoconnect), NULL);

    gboolean ip_changed = FALSE;
    if (gtk_widget_get_visible(A.det_ip_section)) {
        NMSettingIPConfig *ip4 = nm_connection_get_setting_ip4_config(nc);
        if (!ip4) {
            ip4 = NM_SETTING_IP_CONFIG(nm_setting_ip4_config_new());
            g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO, NULL);
            nm_connection_add_setting(nc, NM_SETTING(ip4));
        }
        const char *method = nm_setting_ip_config_get_method(ip4);
        gboolean manual = gtk_toggle_button_get_active(A.det_ip_manual);
        if (manual) {
            char *at = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(A.det_addr))));
            parts = g_strsplit(at, "/", 2);
            g_free(at);
            guint prefix = parts[1] ? (guint)g_ascii_strtoull(parts[1], NULL, 10) : 24;
            NMIPAddress *ia = (*parts[0] && prefix >= 1 && prefix <= 32)
                                  ? nm_ip_address_new(AF_INET, parts[0], prefix, NULL) : NULL;
            if (!ia) { err = "IP 地址无效，格式如 192.168.1.10/24"; goto fail; }
            nm_setting_ip_config_clear_addresses(ip4);
            nm_setting_ip_config_add_address(ip4, ia);
            nm_ip_address_unref(ia);
            char *gw = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(A.det_gw))));
            if (*gw && !nm_utils_ipaddr_valid(AF_INET, gw)) { g_free(gw); err = "网关地址无效"; goto fail; }
            g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_MANUAL,
                         NM_SETTING_IP_CONFIG_GATEWAY, *gw ? gw : NULL, NULL);
            g_free(gw);
        } else if (!g_strcmp0(method, NM_SETTING_IP4_CONFIG_METHOD_MANUAL)) {
            nm_setting_ip_config_clear_addresses(ip4);
            g_object_set(ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO,
                         NM_SETTING_IP_CONFIG_GATEWAY, NULL, NULL);
        }
        dns = g_regex_split_simple("[\\s,;]+", gtk_editable_get_text(GTK_EDITABLE(A.det_dns)), 0, 0);
        nm_setting_ip_config_clear_dns(ip4);
        guint ndns = 0;
        for (char **d = dns; *d; d++) {
            if (!**d) continue;
            if (!nm_utils_ipaddr_valid(AF_INET, *d)) { err = "DNS 地址无效（仅支持 IPv4）"; goto fail; }
            nm_setting_ip_config_add_dns(ip4, *d);
            ndns++;
        }
        g_object_set(ip4, NM_SETTING_IP_CONFIG_IGNORE_AUTO_DNS, ndns > 0 && !manual, NULL);

        NMSettingIPConfig *old = nm_connection_get_setting_ip4_config(NM_CONNECTION(rc));
        ip_changed = !old || !nm_setting_compare(NM_SETTING(old), NM_SETTING(ip4), NM_SETTING_COMPARE_FLAG_EXACT);
    }

    gboolean psk_changed = FALSE;
    if (gtk_widget_get_visible(A.det_psk_row)) {
        const char *pw = gtk_editable_get_text(GTK_EDITABLE(A.det_psk));
        if (*pw && g_strcmp0(pw, A.psk_orig)) {
            NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(nc);
            if ((err = psk_invalid(nm_setting_wireless_security_get_key_mgmt(ws), pw))) goto fail;
            g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_PSK, pw,
                         NM_SETTING_WIRELESS_SECURITY_PSK_FLAGS, NM_SETTING_SECRET_FLAG_NONE, NULL);
            psk_changed = TRUE;
        }
    }

    if (!nm_connection_verify(nc, &e)) {
        set_det_error(e->message);
        g_error_free(e);
        goto out;
    }

    SaveCtx *x = g_new0(SaveCtx, 1);
    x->rc = g_object_ref(rc);
    x->nc = g_object_ref(nc);
    x->psk_changed = psk_changed;
    x->ip_changed = ip_changed;
    gtk_widget_set_sensitive(A.det_save, FALSE);
    set_det_error(NULL);
    nm_remote_connection_update2(rc, nm_connection_to_dbus(nc, NM_CONNECTION_SERIALIZE_ALL),
                                 NM_SETTINGS_UPDATE2_FLAG_TO_DISK, NULL, NULL, save_done, x);
    goto out;
fail:
    set_det_error(err);
out:
    g_strfreev(parts);
    g_strfreev(dns);
    g_object_unref(nc);
}

static void on_det_disconnect(GtkButton *b, gpointer _)
{
    if (A.detail) deactivate(A.detail);
    go_main();
}

static gboolean forget_disarm(gpointer _)
{
    A.forget_armed = 0;
    gtk_button_set_label(GTK_BUTTON(A.det_forget), "忘记");
    return G_SOURCE_REMOVE;
}

static void delete_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    if (!nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(src), res, &e)) {
        notify("删除失败", "%s", e->message);
        g_error_free(e);
    }
}

static void on_det_forget(GtkButton *b, gpointer _)
{
    if (!A.detail) return;
    if (!A.forget_armed) { /* 二次确认 */
        gtk_button_set_label(b, "确认忘记？");
        A.forget_armed = g_timeout_add_seconds(3, forget_disarm, NULL);
        return;
    }
    g_source_remove(A.forget_armed);
    A.forget_armed = 0;
    notify(NULL, "已忘记 %s", nm_connection_get_id(NM_CONNECTION(A.detail)));
    nm_remote_connection_delete_async(A.detail, NULL, delete_done, NULL);
    go_main();
}

/* ───────────────────────── 窗口 ───────────────────────── */

static void go_main(void)
{
    if (A.auth) auth_cancel(NM_SECRET_AGENT_ERROR_USER_CANCELED);
    g_clear_object(&A.detail);
    if (A.forget_armed) { g_source_remove(A.forget_armed); A.forget_armed = 0; }
    gtk_stack_set_visible_child_name(A.stack, "main");
}

static void on_back(GtkButton *b, gpointer _) { go_main(); }

static void on_advanced(GtkButton *b, gpointer _)
{
    GError *e = NULL;
    if (!g_spawn_command_line_async("nm-connection-editor", &e)) {
        notify(NULL, "%s", e->message);
        g_error_free(e);
        return;
    }
    popup_hide();
}

static const char FALLBACK_COLORS[] =
    "@define-color bg rgba(233,234,240,0.9);@define-color surface #d0d5e3;@define-color fg #3760bf;"
    "@define-color subtle #6172b0;@define-color muted #848cb5;@define-color blue #2e7de9;"
    "@define-color green #587539;@define-color red #f52a65;\n";

static const char APP_CSS[] =
    "window.netpop { background: transparent; }\n"
    ".panel { background-color: rgb(from @bg r g b / 0.96); color: @fg; border-radius: 16px;"
    "  border: 1px solid alpha(@muted, 0.30); padding: 12px 4px 12px 12px; }\n"
    ".netpop label { color: inherit; }\n"
    ".glyph { font-family: 'JetBrainsMono Nerd Font', 'Symbols Nerd Font'; font-size: 17px; }\n"
    ".title { font-weight: 700; font-size: 16px; }\n"
    ".section { color: @muted; font-size: 12px; font-weight: 600; margin: 12px 8px 4px 8px; }\n"
    "list { background: transparent; }\n"
    "list row { border-radius: 10px; padding: 7px 8px 7px 10px; margin: 1px 0; background: transparent;"
    "  transition: background-color 120ms ease; outline: none; }\n"
    "list row:hover { background-color: alpha(@fg, 0.07); }\n"
    "list row:focus-visible { background-color: alpha(@blue, 0.14); }\n"
    "list row.active { background-color: alpha(@blue, 0.13); }\n"
    "list row.active .name, list row.active .sig { color: @blue; font-weight: 700; }\n"
    "list row .sig { color: @subtle; min-width: 22px; }\n"
    "list row .lock { color: @muted; font-size: 13px; }\n"
    ".name { font-size: 14px; }\n"
    ".sub, .hint { color: @muted; font-size: 12px; }\n"
    ".placeholder { color: @muted; padding: 28px 0; }\n"
    ".error { color: @red; }\n"
    ".key { color: @muted; font-size: 12.5px; }\n"
    ".val { font-size: 12.5px; font-family: 'JetBrainsMono Nerd Font', monospace; }\n"
    ".card { background-color: alpha(@fg, 0.05); border-radius: 12px; padding: 10px 12px; }\n"
    "button { background: alpha(@fg, 0.07); background-image: none; border: none; box-shadow: none;"
    "  color: @fg; border-radius: 10px; padding: 6px 14px; min-height: 20px; outline: none; }\n"
    "button:hover { background: alpha(@fg, 0.13); }\n"
    "button:focus-visible { box-shadow: 0 0 0 2px alpha(@blue, 0.6); }\n"
    "button.icon { background: transparent; padding: 0; min-width: 30px; min-height: 30px; color: @subtle; }\n"
    "button.icon:hover { background: alpha(@fg, 0.09); }\n"
    "button.suggested { background: @blue; color: #ffffff; font-weight: 600; }\n"
    "button.suggested:hover { background: shade(@blue, 1.08); }\n"
    "button.destructive { color: @red; background: alpha(@red, 0.10); }\n"
    "button.destructive:hover { background: alpha(@red, 0.18); }\n"
    "button:disabled { opacity: 0.5; }\n"
    "button.flat { background: transparent; color: @subtle; }\n"
    "button.flat:hover { background: alpha(@fg, 0.08); }\n"
    ".segmented button { border-radius: 0; padding: 5px 12px; }\n"
    ".segmented button:first-child { border-radius: 9px 0 0 9px; }\n"
    ".segmented button:last-child { border-radius: 0 9px 9px 0; }\n"
    ".segmented button:checked { background: @blue; color: #ffffff; }\n"
    "entry { background: alpha(@fg, 0.06); background-image: none; color: @fg; border: 1px solid alpha(@muted, 0.35);"
    "  border-radius: 9px; box-shadow: none; outline: none; min-height: 34px; padding: 0 10px; caret-color: @blue; }\n"
    "entry:focus-within { border-color: @blue; }\n"
    "entry:disabled { opacity: 0.55; }\n"
    "entry > text > placeholder { color: alpha(@muted, 0.8); }\n"
    "entry image { color: @muted; }\n"
    "switch { background: alpha(@muted, 0.40); background-image: none; border: none; border-radius: 999px;"
    "  box-shadow: none; min-width: 40px; min-height: 22px; padding: 0; outline: none; }\n"
    "switch:checked { background: @blue; }\n"
    "switch slider { background: #ffffff; background-image: none; border: none; box-shadow: 0 1px 2px alpha(black, 0.2);"
    "  border-radius: 999px; min-width: 18px; min-height: 18px; margin: 2px; padding: 0; }\n"
    "switch image { opacity: 0; }\n"
    /* 滚动条：重置主题样式，细条悬浮在右侧 8px 留白槽里 */
    "scrollbar, scrollbar trough, scrollbar range { background: transparent; background-image: none;"
    "  border: none; box-shadow: none; outline: none; padding: 0; margin: 0; }\n"
    "scrollbar slider { background: alpha(@muted, 0.45); background-image: none; border: none; box-shadow: none;"
    "  border-radius: 999px; min-width: 3px; min-height: 28px; margin: 0 1px; transition: all 150ms ease; }\n"
    "scrollbar.hovering slider, scrollbar slider:hover { min-width: 6px; background: alpha(@muted, 0.7); }\n"
    "scrollbar slider:active { background: @blue; }\n"
    "scrollbar button { min-width: 0; min-height: 0; padding: 0; border: none; background: none; }\n"
    "undershoot, overshoot { background: none; }\n"
    ".toast { background-color: rgb(from @fg r g b / 0.94); color: @surface; border-radius: 10px;"
    "  padding: 8px 14px; margin: 0 16px 14px 16px; font-size: 13px; }\n"
    "spinner { color: @blue; }\n"
    "tooltip { background: @surface; color: @fg; border-radius: 8px; }\n";

static void load_css(void)
{
    char *path = g_build_filename(g_get_user_config_dir(), "theme", "current", "waybar.css", NULL);
    char *user = g_build_filename(g_get_user_config_dir(), "netpop", "style.css", NULL);
    char *theme = NULL, *extra = NULL;
    g_file_get_contents(path, &theme, NULL, NULL);
    g_file_get_contents(user, &extra, NULL, NULL);
    char *css = g_strconcat(FALLBACK_COLORS, theme ? theme : "", "\n", APP_CSS, extra ? extra : "", NULL);
    if (g_strcmp0(css, A.css_cache) != 0) { /* 主题未变时不重新解析 */
        gtk_css_provider_load_from_string(A.css, css);
        g_free(A.css_cache);
        A.css_cache = css;
    } else {
        g_free(css);
    }
    g_free(path); g_free(user); g_free(theme); g_free(extra);
}

static void popup_show(void)
{
    load_css();
    if (A.dirty || A.rebuild_id) {
        if (A.rebuild_id) g_source_remove(A.rebuild_id);
        do_rebuild(NULL);
    }
    request_scan(FALSE, 15000);
    A.shown_at = g_get_monotonic_time();
    A.was_active = FALSE;
    gtk_window_present(A.win);
    if (A.dirty) do_rebuild(NULL);
}

static void popup_hide(void)
{
    go_main();
    gtk_widget_set_visible(GTK_WIDGET(A.win), FALSE);
}

static gboolean check_focus_lost(gpointer _)
{
    if (gtk_widget_get_visible(GTK_WIDGET(A.win)) && !gtk_window_is_active(A.win) && !A.auth)
        popup_hide();
    return G_SOURCE_REMOVE;
}

static void on_active_changed(GObject *w, GParamSpec *p, gpointer _)
{
    if (gtk_window_is_active(A.win)) {
        A.was_active = TRUE;
    } else if (A.was_active && g_get_monotonic_time() - A.shown_at > 300000) {
        g_timeout_add(150, check_focus_lost, NULL); /* 点到外面就收起 */
    }
}

static gboolean on_key(GtkEventControllerKey *c, guint keyval, guint code, GdkModifierType mods, gpointer _)
{
    if (keyval != GDK_KEY_Escape) return FALSE;
    const char *page = gtk_stack_get_visible_child_name(A.stack);
    if (g_strcmp0(page, "main")) go_main();
    else popup_hide();
    return TRUE;
}

static gboolean on_close_request(GtkWindow *w, gpointer _)
{
    popup_hide();
    return TRUE;
}

static GtkWidget *page_header(const char *title_text, GtkLabel **title_out, gboolean back)
{
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_bottom(h, 4);
    if (back) gtk_box_append(GTK_BOX(h), glyph_button(G_BACK, "返回 (Esc)", G_CALLBACK(on_back), NULL));
    GtkWidget *t = label(title_text, "title", 0);
    gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_margin_start(t, back ? 2 : 6);
    gtk_box_append(GTK_BOX(h), t);
    if (title_out) *title_out = GTK_LABEL(t);
    return h;
}

static GtkWidget *build_main_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *h = page_header("Wi-Fi", NULL, FALSE);
    GtkWidget *scan = gtk_button_new();
    GtkWidget *sb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    A.scan_glyph = glyph(G_REFRESH, NULL);
    A.scan_spin = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(A.scan_spin));
    gtk_widget_set_visible(A.scan_spin, FALSE);
    gtk_box_append(GTK_BOX(sb), A.scan_glyph);
    gtk_box_append(GTK_BOX(sb), A.scan_spin);
    gtk_button_set_child(GTK_BUTTON(scan), sb);
    gtk_widget_add_css_class(scan, "icon");
    gtk_widget_set_valign(scan, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(scan, "重新扫描");
    g_signal_connect(scan, "clicked", G_CALLBACK(on_scan_clicked), NULL);
    A.scan_btn = scan;
    gtk_box_append(GTK_BOX(h), scan);

    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(sw, 4);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_wifi_switch), NULL);
    A.wifi_switch = GTK_SWITCH(sw);
    gtk_box_append(GTK_BOX(h), sw);
    gtk_box_append(GTK_BOX(page), h);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    A.wifi_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(A.wifi_list, GTK_SELECTION_NONE);
    A.placeholder = GTK_LABEL(label("正在连接 NetworkManager…", "placeholder", 0.5));
    gtk_list_box_set_placeholder(A.wifi_list, GTK_WIDGET(A.placeholder));
    g_signal_connect(A.wifi_list, "row-activated", G_CALLBACK(on_wifi_row), NULL);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(A.wifi_list));

    A.other_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(A.other_section), label("其他连接", "section", 0));
    A.other_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(A.other_list, GTK_SELECTION_NONE);
    g_signal_connect(A.other_list, "row-activated", G_CALLBACK(on_other_row), NULL);
    gtk_box_append(GTK_BOX(A.other_section), GTK_WIDGET(A.other_list));
    gtk_widget_set_visible(A.other_section, FALSE);
    gtk_box_append(GTK_BOX(content), A.other_section);

    GtkWidget *sc = gtk_scrolled_window_new();
    A.scroller = GTK_SCROLLED_WINDOW(sc);
    gtk_scrolled_window_set_policy(A.scroller, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(A.scroller, TRUE);
    gtk_scrolled_window_set_max_content_height(A.scroller, 440);
    gtk_scrolled_window_set_child(A.scroller, content);
    gtk_box_append(GTK_BOX(page), sc);

    GtkWidget *foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(foot, 8);
    GtkWidget *adv = gtk_button_new();
    GtkWidget *ab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(ab), glyph(G_COG, NULL));
    gtk_box_append(GTK_BOX(ab), gtk_label_new("高级设置"));
    gtk_button_set_child(GTK_BUTTON(adv), ab);
    gtk_widget_add_css_class(adv, "flat");
    g_signal_connect(adv, "clicked", G_CALLBACK(on_advanced), NULL);
    gtk_box_append(GTK_BOX(foot), adv);
    gtk_box_append(GTK_BOX(page), foot);
    return page;
}

static GtkWidget *build_auth_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(page), page_header("", &A.auth_title, TRUE));
    A.auth_msg = GTK_LABEL(label("", "hint", 0));
    gtk_label_set_wrap(A.auth_msg, TRUE);
    gtk_widget_set_margin_start(GTK_WIDGET(A.auth_msg), 4);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(A.auth_msg));

    A.auth_entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(A.auth_entry), TRUE);
    g_object_set(A.auth_entry, "placeholder-text", "密码", NULL);
    g_signal_connect(A.auth_entry, "activate", G_CALLBACK(on_auth_submit), NULL);
    gtk_box_append(GTK_BOX(page), A.auth_entry);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btns, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btns, 4);
    gtk_box_append(GTK_BOX(btns), button("取消", NULL, G_CALLBACK(on_back), NULL));
    gtk_box_append(GTK_BOX(btns), button("连接", "suggested", G_CALLBACK(on_auth_submit), NULL));
    gtk_box_append(GTK_BOX(page), btns);
    return page;
}

static GtkWidget *field(const char *k, GtkWidget *w)
{
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(b), label(k, "key", 0));
    gtk_box_append(GTK_BOX(b), w);
    return b;
}

static GtkEntry *entry(const char *placeholder)
{
    GtkWidget *e = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    return GTK_ENTRY(e);
}

static GtkWidget *build_details_page(void)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(outer), page_header("", &A.det_title, TRUE));

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *card = gtk_grid_new();
    A.det_grid = GTK_GRID(card);
    gtk_grid_set_row_spacing(A.det_grid, 5);
    gtk_grid_set_column_spacing(A.det_grid, 14);
    gtk_widget_add_css_class(card, "card");
    gtk_box_append(GTK_BOX(page), card);

    /* 自动连接 */
    GtkWidget *ar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(ar, "card");
    GtkWidget *al = label("自动连接", NULL, 0);
    gtk_widget_set_hexpand(al, TRUE);
    gtk_box_append(GTK_BOX(ar), al);
    A.det_autoconnect = GTK_SWITCH(gtk_switch_new());
    gtk_widget_set_valign(GTK_WIDGET(A.det_autoconnect), GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(ar), GTK_WIDGET(A.det_autoconnect));
    gtk_box_append(GTK_BOX(page), ar);

    /* 密码 */
    A.det_psk = entry("不修改请留空");
    gtk_entry_set_visibility(A.det_psk, FALSE);
    gtk_entry_set_input_purpose(A.det_psk, GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_icon_from_icon_name(A.det_psk, GTK_ENTRY_ICON_SECONDARY, "view-reveal-symbolic");
    gtk_entry_set_icon_tooltip_text(A.det_psk, GTK_ENTRY_ICON_SECONDARY, "显示密码");
    g_signal_connect(A.det_psk, "icon-press", G_CALLBACK(on_psk_icon), NULL);
    A.det_psk_row = field("密码", GTK_WIDGET(A.det_psk));
    gtk_box_append(GTK_BOX(page), A.det_psk_row);

    /* IPv4 */
    A.det_ip_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *mh = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *ml = label("IPv4", "key", 0);
    gtk_widget_set_hexpand(ml, TRUE);
    gtk_box_append(GTK_BOX(mh), ml);
    GtkWidget *seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(seg, "segmented");
    A.det_ip_auto = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("自动 (DHCP)"));
    A.det_ip_manual = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("手动"));
    gtk_toggle_button_set_group(A.det_ip_manual, A.det_ip_auto);
    g_signal_connect(A.det_ip_manual, "toggled", G_CALLBACK(on_ip_mode), NULL);
    gtk_box_append(GTK_BOX(seg), GTK_WIDGET(A.det_ip_auto));
    gtk_box_append(GTK_BOX(seg), GTK_WIDGET(A.det_ip_manual));
    gtk_box_append(GTK_BOX(mh), seg);
    gtk_box_append(GTK_BOX(A.det_ip_section), mh);

    A.det_ip_fields = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    A.det_addr = entry("192.168.1.100/24");
    A.det_gw = entry("192.168.1.1");
    gtk_box_append(GTK_BOX(A.det_ip_fields), field("地址 / 前缀", GTK_WIDGET(A.det_addr)));
    gtk_box_append(GTK_BOX(A.det_ip_fields), field("网关", GTK_WIDGET(A.det_gw)));
    gtk_box_append(GTK_BOX(A.det_ip_section), A.det_ip_fields);
    A.det_dns = entry("留空则使用自动获取的 DNS");
    gtk_box_append(GTK_BOX(A.det_ip_section), field("DNS（逗号分隔）", GTK_WIDGET(A.det_dns)));
    gtk_box_append(GTK_BOX(page), A.det_ip_section);

    A.det_err = GTK_LABEL(label("", "error", 0));
    gtk_label_set_wrap(A.det_err, TRUE);
    gtk_widget_set_visible(GTK_WIDGET(A.det_err), FALSE);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(A.det_err));

    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), 720);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), page);
    gtk_box_append(GTK_BOX(outer), sc);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(btns, 4);
    A.det_forget = button("忘记", "destructive", G_CALLBACK(on_det_forget), NULL);
    gtk_box_append(GTK_BOX(btns), A.det_forget);
    GtkWidget *sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(sp, TRUE);
    gtk_box_append(GTK_BOX(btns), sp);
    A.det_disconnect = button("断开", NULL, G_CALLBACK(on_det_disconnect), NULL);
    gtk_box_append(GTK_BOX(btns), A.det_disconnect);
    A.det_save = button("保存", "suggested", G_CALLBACK(on_det_save), NULL);
    gtk_box_append(GTK_BOX(btns), A.det_save);
    gtk_box_append(GTK_BOX(outer), btns);
    return outer;
}

static void build_ui(void)
{
    A.css = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(A.css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);

    A.win = GTK_WINDOW(gtk_application_window_new(A.app));
    gtk_window_set_title(A.win, "Netpop");
    gtk_window_set_decorated(A.win, FALSE);
    gtk_window_set_resizable(A.win, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(A.win), "netpop");

#ifdef HAVE_LAYER_SHELL
    if (gtk_layer_is_supported()) {
        A.layer = TRUE;
        gtk_layer_init_for_window(A.win);
        gtk_layer_set_namespace(A.win, "netpop");
        gtk_layer_set_layer(A.win, GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_anchor(A.win, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(A.win, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        const char *mt = g_getenv("NETPOP_MARGIN_TOP"), *mr = g_getenv("NETPOP_MARGIN_RIGHT");
        gtk_layer_set_margin(A.win, GTK_LAYER_SHELL_EDGE_TOP, mt ? atoi(mt) : 6);
        gtk_layer_set_margin(A.win, GTK_LAYER_SHELL_EDGE_RIGHT, mr ? atoi(mr) : 8);
        gtk_layer_set_keyboard_mode(A.win, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    }
#endif

    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(panel, "panel");
    gtk_widget_set_size_request(panel, 368, -1);

    A.stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_vhomogeneous(A.stack, FALSE);
    gtk_stack_set_transition_type(A.stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(A.stack, 90);
    gtk_stack_add_named(A.stack, build_main_page(), "main");
    gtk_stack_add_named(A.stack, build_auth_page(), "auth");
    gtk_stack_add_named(A.stack, build_details_page(), "details");
    gtk_box_append(GTK_BOX(panel), GTK_WIDGET(A.stack));
    /* 面板右内边距只留 4px，右侧 8px 留给滚动条；其余内容统一缩进 8px 对齐 */
    for (GtkWidget *pg = gtk_widget_get_first_child(GTK_WIDGET(A.stack)); pg; pg = gtk_widget_get_next_sibling(pg))
        for (GtkWidget *c = gtk_widget_get_first_child(pg); c; c = gtk_widget_get_next_sibling(c)) {
            GtkWidget *target = c;
            if (GTK_IS_SCROLLED_WINDOW(c)) {
                target = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(c));
                if (GTK_IS_VIEWPORT(target)) target = gtk_viewport_get_child(GTK_VIEWPORT(target));
            }
            gtk_widget_set_margin_end(target, 8);
        }

    GtkWidget *ov = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ov), panel);
    A.toast_rev = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(A.toast_rev, GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_valign(GTK_WIDGET(A.toast_rev), GTK_ALIGN_END);
    gtk_widget_set_halign(GTK_WIDGET(A.toast_rev), GTK_ALIGN_CENTER);
    gtk_widget_set_can_target(GTK_WIDGET(A.toast_rev), FALSE);
    A.toast = GTK_LABEL(label("", "toast", 0.5));
    gtk_label_set_wrap(A.toast, TRUE);
    gtk_revealer_set_child(A.toast_rev, GTK_WIDGET(A.toast));
    gtk_overlay_add_overlay(GTK_OVERLAY(ov), GTK_WIDGET(A.toast_rev));
    gtk_window_set_child(A.win, ov);

    GtkEventController *kc = gtk_event_controller_key_new();
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(GTK_WIDGET(A.win), kc);
    g_signal_connect(A.win, "close-request", G_CALLBACK(on_close_request), NULL);
    g_signal_connect(A.win, "notify::is-active", G_CALLBACK(on_active_changed), NULL);
    load_css();
}

/* ───────────────────────── NM 客户端 ───────────────────────── */

static void hook_ac(NMActiveConnection *ac)
{
    g_signal_connect(ac, "notify::state", G_CALLBACK(queue_rebuild), NULL);
}

static void hook_device(NMDevice *d)
{
    g_signal_connect(d, "notify::state", G_CALLBACK(queue_rebuild), NULL);
    if (NM_IS_DEVICE_WIFI(d)) {
        g_signal_connect(d, "access-point-added", G_CALLBACK(queue_rebuild), NULL);
        g_signal_connect(d, "access-point-removed", G_CALLBACK(queue_rebuild), NULL);
        g_signal_connect(d, "notify::active-access-point", G_CALLBACK(queue_rebuild), NULL);
        g_signal_connect(d, "notify::last-scan", G_CALLBACK(on_last_scan), NULL);
    }
}

static void on_device_added(NMClient *c, NMDevice *d, gpointer _) { hook_device(d); queue_rebuild(); }
static void on_ac_added(NMClient *c, NMActiveConnection *ac, gpointer _) { hook_ac(ac); queue_rebuild(); }

static void client_ready(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    A.client = nm_client_new_finish(res, &e);
    if (!A.client) {
        gtk_label_set_text(A.placeholder, "无法连接 NetworkManager");
        g_warning("%s", e->message);
        g_error_free(e);
        return;
    }
    const char *sigs[] = { "notify::wireless-enabled", "notify::wireless-hardware-enabled",
                           "connection-added", "connection-removed", "active-connection-removed",
                           "device-removed", "notify::nm-running" };
    for (guint i = 0; i < G_N_ELEMENTS(sigs); i++)
        g_signal_connect(A.client, sigs[i], G_CALLBACK(queue_rebuild), NULL);
    g_signal_connect(A.client, "device-added", G_CALLBACK(on_device_added), NULL);
    g_signal_connect(A.client, "active-connection-added", G_CALLBACK(on_ac_added), NULL);
    const GPtrArray *devs = nm_client_get_devices(A.client);
    for (guint i = 0; i < devs->len; i++) hook_device(devs->pdata[i]);
    const GPtrArray *acs = nm_client_get_active_connections(A.client);
    for (guint i = 0; i < acs->len; i++) hook_ac(acs->pdata[i]);

    if (!g_getenv("NETPOP_NO_AGENT"))
        g_async_initable_new_async(netpop_agent_get_type(), G_PRIORITY_DEFAULT, NULL, agent_ready, NULL,
                                   NM_SECRET_AGENT_OLD_IDENTIFIER, "dev.netpop.agent",
                                   NM_SECRET_AGENT_OLD_AUTO_REGISTER, TRUE, NULL);

    A.dirty = TRUE;
    if (gtk_widget_get_visible(GTK_WIDGET(A.win))) {
        do_rebuild(NULL);
        request_scan(FALSE, 15000);
    }
}

/* ───────────────────────── 应用 ───────────────────────── */

static void on_startup(GApplication *app, gpointer _)
{
    g_application_hold(app); /* 常驻：隐藏后再打开是瞬时的 */
    build_ui();
    nm_client_new_async(NULL, client_ready, NULL);
}

static int on_cmdline(GApplication *app, GApplicationCommandLine *cl, gpointer _)
{
    int argc;
    char **argv = g_application_command_line_get_arguments(cl, &argc);
    const char *cmd = argc > 1 ? argv[1] : "--toggle";
    gboolean visible = gtk_widget_get_visible(GTK_WIDGET(A.win));

    if (!strcmp(cmd, "--quit")) {
        if (A.auth) auth_cancel(NM_SECRET_AGENT_ERROR_USER_CANCELED);
        g_application_release(app);
        g_application_quit(app);
    } else if (!strcmp(cmd, "--daemon")) {
        /* 仅启动/保持常驻 */
    } else if (!strcmp(cmd, "--show")) {
        popup_show();
    } else if (!strcmp(cmd, "--hide")) {
        popup_hide();
    } else if (!strcmp(cmd, "--toggle")) {
        if (visible) popup_hide();
        else popup_show();
    } else {
        g_application_command_line_printerr(cl, "用法: netpop [--toggle|--show|--hide|--daemon|--quit]\n");
        g_strfreev(argv);
        return 1;
    }
    g_strfreev(argv);
    return 0;
}

int main(int argc, char **argv)
{
    A.app = gtk_application_new(APP_ID, G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(A.app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(A.app, "command-line", G_CALLBACK(on_cmdline), NULL);
    int r = g_application_run(G_APPLICATION(A.app), argc, argv);
    g_object_unref(A.app);
    return r;
}
