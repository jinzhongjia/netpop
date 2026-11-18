#include "np-secrets.h"
#include "np-conn-model.h"

#include <string.h>

static NMConnection *wifi(NpSecType sec)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    np_set_str(&m->id, "conn-id");
    np_set_str(&m->ssid, "Office");
    m->security = sec;
    if (sec == NP_SEC_WPA_PSK) np_set_str(&m->psk, "password123");
    if (sec == NP_SEC_WEP) np_set_str(&m->wep_key, "0123456789");
    NMConnection *c = np_conn_model_create_connection(m, NULL);
    g_assert_nonnull(c);
    return c;
}

static NMConnection *eap(NpEapMethod method)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    np_set_str(&m->ssid, "Corp");
    m->security = NP_SEC_EAP;
    m->eap = method;
    np_set_str(&m->identity, "alice");
    if (method == NP_EAP_TLS) {
        char *cert = g_test_build_filename(G_TEST_DIST, "data", "client.pem", NULL);
        char *key = g_test_build_filename(G_TEST_DIST, "data", "client.key", NULL);
        np_set_str(&m->client_cert, cert);
        np_set_str(&m->private_key, key);
        g_free(cert);
        g_free(key);
    }
    NMConnection *c = np_conn_model_create_connection(m, NULL);
    g_assert_nonnull(c);
    return c;
}

static NMConnection *vpn(const char *data)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_VPN_SETTING_NAME);
    np_set_str(&m->id, "Work VPN");
    np_set_str(&m->vpn_service, "org.freedesktop.NetworkManager.openvpn");
    np_set_str(&m->vpn_data, data);
    NMConnection *c = np_conn_model_create_connection(m, NULL);
    g_assert_nonnull(c);
    return c;
}

static void test_psk(void)
{
    NMConnection *c = wifi(NP_SEC_WPA_PSK);
    GError *e = NULL;
    g_autoptr(NpSecretRequest) r = np_secret_request_new(c, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL, FALSE, &e);
    g_assert_no_error(e);
    g_assert_cmpstr(r->title, ==, "Office"); /* Wi-Fi 用 SSID 作为标题 */
    g_assert_false(r->retry);
    g_assert_cmpuint(r->fields->len, ==, 1);
    NpSecretField *f = r->fields->pdata[0];
    g_assert_cmpstr(f->key, ==, "psk");
    g_assert_true(f->secret);
    g_assert_false(np_secret_request_is_complete(r));

    np_secret_request_set(r, "psk", "hunter22");
    g_assert_true(np_secret_request_is_complete(r));
    GVariant *v = g_variant_ref_sink(np_secret_request_to_variant(r));
    g_assert_cmpstr(g_variant_get_type_string(v), ==, "a{sa{sv}}");
    GVariant *s = g_variant_lookup_value(v, "802-11-wireless-security", G_VARIANT_TYPE("a{sv}"));
    const char *psk = NULL;
    g_assert_true(g_variant_lookup(s, "psk", "&s", &psk));
    g_assert_cmpstr(psk, ==, "hunter22");
    g_variant_unref(s);
    g_variant_unref(v);
    g_object_unref(c);
}

static void test_retry_message(void)
{
    NMConnection *c = wifi(NP_SEC_WPA_PSK);
    g_autoptr(NpSecretRequest) r = np_secret_request_new(c, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL, TRUE, NULL);
    g_assert_true(r->retry);
    g_assert_nonnull(strstr(r->message, "错误"));
    g_object_unref(c);
}

static void test_wep(void)
{
    NMConnection *c = wifi(NP_SEC_WEP);
    g_autoptr(NpSecretRequest) r = np_secret_request_new(c, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL, FALSE, NULL);
    g_assert_cmpuint(r->fields->len, ==, 1);
    g_assert_cmpstr(((NpSecretField *)r->fields->pdata[0])->key, ==, "wep-key0");
    g_object_unref(c);
}

static void test_8021x(void)
{
    NMConnection *c = eap(NP_EAP_PEAP);
    g_autoptr(NpSecretRequest) r = np_secret_request_new(c, NM_SETTING_802_1X_SETTING_NAME, NULL, FALSE, NULL);
    g_assert_nonnull(r);
    g_assert_cmpstr(((NpSecretField *)r->fields->pdata[0])->key, ==, "password");
    g_assert_nonnull(strstr(r->message, "alice"));
    g_object_unref(c);

    c = eap(NP_EAP_TLS);
    g_autoptr(NpSecretRequest) t = np_secret_request_new(c, NM_SETTING_802_1X_SETTING_NAME, NULL, FALSE, NULL);
    g_assert_cmpstr(((NpSecretField *)t->fields->pdata[0])->key, ==, "private-key-password");
    g_object_unref(c);
}

static void test_vpn_hints(void)
{
    NMConnection *c = vpn("remote = x\n");
    const char *hints[] = { "x-vpn-message:请输入一次性验证码", "password", "otp", NULL };
    g_autoptr(NpSecretRequest) r = np_secret_request_new(c, NM_SETTING_VPN_SETTING_NAME, hints, FALSE, NULL);
    g_assert_cmpstr(r->title, ==, "Work VPN");
    g_assert_cmpstr(r->message, ==, "请输入一次性验证码");
    g_assert_cmpuint(r->fields->len, ==, 2);
    g_assert_cmpstr(((NpSecretField *)r->fields->pdata[0])->label, ==, "密码");
    g_assert_cmpstr(((NpSecretField *)r->fields->pdata[1])->label, ==, "otp");

    np_secret_request_set(r, "password", "pw");
    /* otp 为空：不完整，且不会出现在应答里 */
    g_assert_false(np_secret_request_is_complete(r));
    GVariant *v = g_variant_ref_sink(np_secret_request_to_variant(r));
    GVariant *s = g_variant_lookup_value(v, "vpn", G_VARIANT_TYPE("a{sv}"));
    GVariant *sec = g_variant_lookup_value(s, "secrets", G_VARIANT_TYPE("a{ss}"));
    g_assert_cmpuint(g_variant_n_children(sec), ==, 1);
    const char *pw = NULL;
    g_assert_true(g_variant_lookup(sec, "password", "&s", &pw));
    g_assert_cmpstr(pw, ==, "pw");
    g_variant_unref(sec);
    g_variant_unref(s);
    g_variant_unref(v);
    g_object_unref(c);
}

static void test_vpn_flags_scan(void)
{
    /* 没有 hints：从 *-flags 推断；NOT_REQUIRED(4) 的跳过 */
    NMConnection *c = vpn("password-flags = 1\ncert-pass-flags = 4\nhttp-proxy-password-flags = 0\n");
    g_autoptr(NpSecretRequest) r = np_secret_request_new(c, NM_SETTING_VPN_SETTING_NAME, NULL, FALSE, NULL);
    g_assert_cmpuint(r->fields->len, ==, 2);
    g_assert_nonnull(np_secret_request_get_field(r, "password"));
    g_assert_nonnull(np_secret_request_get_field(r, "http-proxy-password"));
    g_assert_null(np_secret_request_get_field(r, "cert-pass"));
    g_object_unref(c);

    /* 什么都没有：退回到 password */
    c = vpn("remote = x\n");
    g_autoptr(NpSecretRequest) d = np_secret_request_new(c, NM_SETTING_VPN_SETTING_NAME, NULL, FALSE, NULL);
    g_assert_cmpuint(d->fields->len, ==, 1);
    g_assert_cmpstr(((NpSecretField *)d->fields->pdata[0])->key, ==, "password");
    g_object_unref(c);
}

static void test_unsupported(void)
{
    NMConnection *c = wifi(NP_SEC_OWE);
    GError *e = NULL;
    g_assert_null(np_secret_request_new(c, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NULL, FALSE, &e));
    g_assert_error(e, NP_ERROR, NP_ERROR_UNSUPPORTED);
    g_clear_error(&e);
    g_assert_null(np_secret_request_new(c, "wireguard", NULL, FALSE, &e));
    g_assert_error(e, NP_ERROR, NP_ERROR_UNSUPPORTED);
    g_clear_error(&e);
    g_object_unref(c);
}

static void test_fill(void)
{
    g_autoptr(NpSecretRequest) r = np_secret_request_new_psk("Cafe", "此网络需要密码");
    GHashTable *t = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(t, "psk", "from-keyring");
    g_hash_table_insert(t, "other", "ignored");
    np_secret_request_fill(r, t);
    g_hash_table_unref(t);
    g_assert_true(np_secret_request_is_complete(r));
    g_assert_cmpstr(np_secret_request_get_field(r, "psk")->value, ==, "from-keyring");
    np_secret_request_fill(r, NULL);
    np_secret_request_set(r, "missing", "x"); /* 不存在的字段：忽略 */
}

static void test_collect(void)
{
    NMConnection *c = wifi(NP_SEC_WPA_PSK);
    NMSetting *ws = NM_SETTING(nm_connection_get_setting_wireless_security(c));
    GPtrArray *items = np_secrets_collect(c, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_cmpuint(items->len, ==, 0); /* 系统保存的不收集 */
    g_ptr_array_unref(items);

    g_assert_true(nm_setting_set_secret_flags(ws, "psk", NM_SETTING_SECRET_FLAG_AGENT_OWNED, NULL));
    items = np_secrets_collect(c, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_cmpuint(items->len, ==, 1);
    NpSecretItem *it = items->pdata[0];
    g_assert_cmpstr(it->setting_name, ==, "802-11-wireless-security");
    g_assert_cmpstr(it->key, ==, "psk");
    g_assert_cmpstr(it->value, ==, "password123");
    g_ptr_array_unref(items);
    g_object_unref(c);

    c = vpn("password-flags = 1\nother-flags = 0\n");
    NMSettingVpn *v = nm_connection_get_setting_vpn(c);
    nm_setting_vpn_add_secret(v, "password", "vpnpw");
    nm_setting_vpn_add_secret(v, "other", "sys");
    items = np_secrets_collect(c, NM_SETTING_SECRET_FLAG_AGENT_OWNED);
    g_assert_cmpuint(items->len, ==, 1);
    it = items->pdata[0];
    g_assert_cmpstr(it->setting_name, ==, "vpn");
    g_assert_cmpstr(it->key, ==, "password");
    g_assert_cmpstr(it->value, ==, "vpnpw");
    g_ptr_array_unref(items);
    g_object_unref(c);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/secrets/psk", test_psk);
    g_test_add_func("/secrets/retry-message", test_retry_message);
    g_test_add_func("/secrets/wep", test_wep);
    g_test_add_func("/secrets/8021x", test_8021x);
    g_test_add_func("/secrets/vpn-hints", test_vpn_hints);
    g_test_add_func("/secrets/vpn-flags-scan", test_vpn_flags_scan);
    g_test_add_func("/secrets/unsupported", test_unsupported);
    g_test_add_func("/secrets/fill", test_fill);
    g_test_add_func("/secrets/collect", test_collect);
    return g_test_run();
}
