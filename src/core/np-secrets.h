/* np-secrets.h — Secret Agent 的决策逻辑（与 D-Bus / 界面无关）
 *
 * NetworkManager 向 agent 请求某个设置（setting）的密钥时，这里决定：
 *   - 需要向用户询问哪些字段；
 *   - 如何把用户的输入打包成 NM 期望的 a{sa{sv}} 应答；
 *   - 哪些密钥属于 agent 保存（AGENT_OWNED），需要写入钥匙环。
 */
#pragma once

#include "np-util.h"

G_BEGIN_DECLS

typedef struct {
    char    *key;
    char    *label;
    gboolean secret; /* 输入时遮挡 */
    char    *value;
} NpSecretField;

typedef struct {
    char      *setting_name;
    char      *title;   /* 连接名或 SSID */
    char      *message; /* 提示语 */
    gboolean   retry;   /* NM 要求重新输入（上次的密钥错误） */
    GPtrArray *fields;  /* NpSecretField* */
} NpSecretRequest;

/* 不支持的请求返回 NULL 并设置 NP_ERROR_UNSUPPORTED */
NpSecretRequest *np_secret_request_new(NMConnection *connection, const char *setting_name,
                                       const char *const *hints, gboolean request_new, GError **error);
/* 新网络快速连接时使用的单字段请求（psk） */
NpSecretRequest *np_secret_request_new_psk(const char *title, const char *message);
void             np_secret_request_free(NpSecretRequest *r);

NpSecretField *np_secret_request_get_field(NpSecretRequest *r, const char *key);
void           np_secret_request_set(NpSecretRequest *r, const char *key, const char *value);
void           np_secret_request_fill(NpSecretRequest *r, GHashTable *values); /* key → value */
gboolean       np_secret_request_is_complete(const NpSecretRequest *r);
/* 返回浮动引用的 a{sa{sv}}；空值字段不包含在内 */
GVariant      *np_secret_request_to_variant(const NpSecretRequest *r);

/* 连接中带有 flag（如 NM_SETTING_SECRET_FLAG_AGENT_OWNED）且有值的全部密钥 */
typedef struct {
    char *setting_name;
    char *key;
    char *value;
} NpSecretItem;

void       np_secret_item_free(NpSecretItem *item);
GPtrArray *np_secrets_collect(NMConnection *connection, NMSettingSecretFlags flag);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NpSecretRequest, np_secret_request_free)

G_END_DECLS
