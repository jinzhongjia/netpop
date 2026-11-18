#include "np-secrets.h"

#include <string.h>

#define VPN_MESSAGE_HINT "x-vpn-message:"

static void field_free(NpSecretField *f)
{
    g_free(f->key);
    g_free(f->label);
    g_free(f->value);
    g_free(f);
}

static NpSecretRequest *request_alloc(const char *setting, const char *title)
{
    NpSecretRequest *r = g_new0(NpSecretRequest, 1);
    r->setting_name = g_strdup(setting);
    r->title = g_strdup(title);
    r->fields = g_ptr_array_new_with_free_func((GDestroyNotify)field_free);
    return r;
}

static void add_field(NpSecretRequest *r, const char *key, const char *label, gboolean secret)
{
    if (np_secret_request_get_field(r, key)) return;
    NpSecretField *f = g_new0(NpSecretField, 1);
    f->key = g_strdup(key);
    f->label = g_strdup(label);
    f->secret = secret;
    g_ptr_array_add(r->fields, f);
}

void np_secret_request_free(NpSecretRequest *r)
{
    if (!r) return;
    g_free(r->setting_name);
    g_free(r->title);
    g_free(r->message);
    g_ptr_array_unref(r->fields);
    g_free(r);
}

NpSecretField *np_secret_request_get_field(NpSecretRequest *r, const char *key)
{
    for (guint i = 0; i < r->fields->len; i++) {
        NpSecretField *f = r->fields->pdata[i];
        if (!g_strcmp0(f->key, key)) return f;
    }
    return NULL;
}

void np_secret_request_set(NpSecretRequest *r, const char *key, const char *value)
{
    NpSecretField *f = np_secret_request_get_field(r, key);
    if (!f) return;
    g_free(f->value);
    f->value = g_strdup(value);
}

void np_secret_request_fill(NpSecretRequest *r, GHashTable *values)
{
    if (!values) return;
    for (guint i = 0; i < r->fields->len; i++) {
        NpSecretField *f = r->fields->pdata[i];
        const char *v = g_hash_table_lookup(values, f->key);
        if (v && *v) {
            g_free(f->value);
            f->value = g_strdup(v);
        }
    }
}

gboolean np_secret_request_is_complete(const NpSecretRequest *r)
{
    if (!r->fields->len) return FALSE;
    for (guint i = 0; i < r->fields->len; i++) {
        NpSecretField *f = r->fields->pdata[i];
        if (!f->value || !*f->value) return FALSE;
    }
    return TRUE;
}

static const char *vpn_label(const char *key)
{
    static const struct { const char *key, *label; } map[] = {
        { "password", "密码" },
        { "cert-pass", "证书密码" },
        { "http-proxy-password", "HTTP 代理密码" },
        { "Xauth password", "XAuth 密码" },
        { "IPSec secret", "IPSec 预共享密钥" },
        { "pskvalue", "预共享密钥" },
        { "pin", "PIN" },
    };
    for (guint i = 0; i < G_N_ELEMENTS(map); i++)
        if (!strcmp(map[i].key, key)) return map[i].label;
    return key;
}

typedef struct {
    NpSecretRequest *r;
    NMSettingVpn *vpn;
} VpnScan;

static void vpn_scan_flags(const char *key, const char *value, gpointer data)
{
    VpnScan *s = data;
    if (!g_str_has_suffix(key, "-flags")) return;
    char *name = g_strndup(key, strlen(key) - strlen("-flags"));
    guint64 flags = g_ascii_strtoull(value ? value : "0", NULL, 10);
    if (*name && !(flags & NM_SETTING_SECRET_FLAG_NOT_REQUIRED))
        add_field(s->r, name, vpn_label(name), TRUE);
    g_free(name);
}

static char *conn_title(NMConnection *c)
{
    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    if (sw && nm_setting_wireless_get_ssid(sw)) return np_ssid_to_display(nm_setting_wireless_get_ssid(sw));
    return g_strdup(nm_connection_get_id(c) ? nm_connection_get_id(c) : "");
}

NpSecretRequest *np_secret_request_new(NMConnection *c, const char *setting_name,
                                       const char *const *hints, gboolean request_new, GError **error)
{
    char *title = conn_title(c);
    NpSecretRequest *r = request_alloc(setting_name, title);
    g_free(title);
    r->retry = request_new;

    if (!g_strcmp0(setting_name, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME)) {
        NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
        const char *km = ws ? nm_setting_wireless_security_get_key_mgmt(ws) : NULL;
        if (!g_strcmp0(km, "wpa-psk") || !g_strcmp0(km, "sae")) {
            add_field(r, NM_SETTING_WIRELESS_SECURITY_PSK, "密码", TRUE);
        } else if (!g_strcmp0(km, "none")) {
            char key[] = "wep-key0";
            key[7] = (char)('0' + MIN(nm_setting_wireless_security_get_wep_tx_keyidx(ws), 3u));
            add_field(r, key, "WEP 密钥", TRUE);
        }
        r->message = g_strdup(request_new ? "密码错误，请重新输入" : "此网络需要密码");
    } else if (!g_strcmp0(setting_name, NM_SETTING_802_1X_SETTING_NAME)) {
        NMSetting8021x *x = nm_connection_get_setting_802_1x(c);
        const char *eap = x && nm_setting_802_1x_get_num_eap_methods(x) ? nm_setting_802_1x_get_eap_method(x, 0) : NULL;
        if (!g_strcmp0(eap, "tls")) add_field(r, NM_SETTING_802_1X_PRIVATE_KEY_PASSWORD, "私钥密码", TRUE);
        else if (x) add_field(r, NM_SETTING_802_1X_PASSWORD, "密码", TRUE);
        const char *id = x ? nm_setting_802_1x_get_identity(x) : NULL;
        r->message = id ? g_strdup_printf(request_new ? "认证失败，请重新输入 %s 的密码" : "请输入 %s 的密码", id)
                        : g_strdup(request_new ? "认证失败，请重新输入" : "此网络需要企业认证");
    } else if (!g_strcmp0(setting_name, NM_SETTING_VPN_SETTING_NAME)) {
        NMSettingVpn *vpn = nm_connection_get_setting_vpn(c);
        for (const char *const *h = hints; h && *h; h++) {
            if (g_str_has_prefix(*h, VPN_MESSAGE_HINT)) {
                g_free(r->message);
                r->message = g_strdup(*h + strlen(VPN_MESSAGE_HINT));
            } else if (**h) {
                add_field(r, *h, vpn_label(*h), TRUE);
            }
        }
        if (!r->fields->len && vpn) {
            VpnScan s = { r, vpn };
            nm_setting_vpn_foreach_data_item(vpn, vpn_scan_flags, &s);
        }
        if (!r->fields->len) add_field(r, "password", "密码", TRUE);
        if (!r->message) r->message = g_strdup(request_new ? "认证失败，请重新输入" : "VPN 需要认证");
    }

    if (!r->fields->len) {
        g_set_error(error, NP_ERROR, NP_ERROR_UNSUPPORTED, "不支持的密钥请求：%s", setting_name);
        np_secret_request_free(r);
        return NULL;
    }
    return r;
}

NpSecretRequest *np_secret_request_new_psk(const char *title, const char *message)
{
    NpSecretRequest *r = request_alloc(NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, title);
    r->message = g_strdup(message);
    add_field(r, NM_SETTING_WIRELESS_SECURITY_PSK, "密码", TRUE);
    return r;
}

GVariant *np_secret_request_to_variant(const NpSecretRequest *r)
{
    GVariantBuilder outer, inner;
    g_variant_builder_init(&outer, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_init(&inner, G_VARIANT_TYPE("a{sv}"));
    gboolean vpn = !g_strcmp0(r->setting_name, NM_SETTING_VPN_SETTING_NAME);

    if (vpn) {
        GVariantBuilder sec;
        g_variant_builder_init(&sec, G_VARIANT_TYPE("a{ss}"));
        for (guint i = 0; i < r->fields->len; i++) {
            NpSecretField *f = r->fields->pdata[i];
            if (f->value && *f->value) g_variant_builder_add(&sec, "{ss}", f->key, f->value);
        }
        g_variant_builder_add(&inner, "{sv}", NM_SETTING_VPN_SECRETS, g_variant_builder_end(&sec));
    } else {
        for (guint i = 0; i < r->fields->len; i++) {
            NpSecretField *f = r->fields->pdata[i];
            if (f->value && *f->value) g_variant_builder_add(&inner, "{sv}", f->key, g_variant_new_string(f->value));
        }
    }
    g_variant_builder_add(&outer, "{sa{sv}}", r->setting_name, &inner);
    return g_variant_builder_end(&outer);
}

/* ── 收集 agent 保存的密钥 ── */

void np_secret_item_free(NpSecretItem *it)
{
    if (!it) return;
    g_free(it->setting_name);
    g_free(it->key);
    if (it->value) memset(it->value, 0, strlen(it->value));
    g_free(it->value);
    g_free(it);
}

typedef struct {
    GPtrArray *out;
    NMSettingSecretFlags flag;
} Collect;

static void add_item(Collect *col, NMSetting *s, const char *key, const char *value)
{
    NMSettingSecretFlags f = NM_SETTING_SECRET_FLAG_NONE;
    if (!value || !*value || !nm_setting_get_secret_flags(s, key, &f, NULL) || !(f & col->flag)) return;
    NpSecretItem *it = g_new0(NpSecretItem, 1);
    it->setting_name = g_strdup(nm_setting_get_name(s));
    it->key = g_strdup(key);
    it->value = g_strdup(value);
    g_ptr_array_add(col->out, it);
}

typedef struct {
    Collect *col;
    NMSetting *setting;
} VpnCollect;

static void vpn_secret_cb(const char *key, const char *value, gpointer data)
{
    VpnCollect *vc = data;
    add_item(vc->col, vc->setting, key, value);
}

static void each_value(NMSetting *s, const char *key, const GValue *value, GParamFlags flags, gpointer data)
{
    Collect *col = data;
    if (!(flags & NM_SETTING_PARAM_SECRET)) return;
    if (NM_IS_SETTING_VPN(s) && !strcmp(key, NM_SETTING_VPN_SECRETS)) {
        VpnCollect vc = { col, s };
        nm_setting_vpn_foreach_secret(NM_SETTING_VPN(s), vpn_secret_cb, &vc);
    } else if (G_VALUE_HOLDS_STRING(value)) {
        add_item(col, s, key, g_value_get_string(value));
    }
}

GPtrArray *np_secrets_collect(NMConnection *c, NMSettingSecretFlags flag)
{
    Collect col = { g_ptr_array_new_with_free_func((GDestroyNotify)np_secret_item_free), flag };
    nm_connection_for_each_setting_value(c, each_value, &col);
    return col.out;
}
