#include "np-conn-model.h"

#include <sys/socket.h>

static char *data_path(const char *name)
{
    return g_test_build_filename(G_TEST_DIST, "data", name, NULL);
}

static NpConnModel *wifi_model(const char *ssid, NpSecType sec, const char *psk)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    np_set_str(&m->id, ssid);
    np_set_str(&m->ssid, ssid);
    np_set_str(&m->psk, psk);
    m->security = sec;
    return m;
}

static NMConnection *create_ok(const NpConnModel *m)
{
    GError *e = NULL;
    NMConnection *c = np_conn_model_create_connection(m, &e);
    g_assert_no_error(e);
    g_assert_nonnull(c);
    g_assert_true(nm_connection_verify(c, NULL));
    return c;
}

static void assert_create_fails(const NpConnModel *m)
{
    GError *e = NULL;
    NMConnection *c = np_conn_model_create_connection(m, &e);
    g_assert_null(c);
    g_assert_nonnull(e);
    g_test_message("expected error: %s", e->message);
    g_error_free(e);
}

/* ── 通用 ── */

static void test_choice_index(void)
{
    g_assert_cmpuint(np_choice_index(np_ip4_method_choices, NP_IP4_METHOD_N, "shared"), ==, 3);
    g_assert_cmpuint(np_choice_index(np_mac_choices, NP_MAC_N, NULL), ==, 0);
    g_assert_cmpuint(np_choice_index(np_mac_choices, NP_MAC_N, "random"), ==, 3);
    g_assert_cmpuint(np_choice_index(np_mac_choices, NP_MAC_N, "00:11:22:33:44:55"), ==, NP_MAC_N);
}

static void test_defaults(void)
{
    g_autoptr(NpConnModel) w = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    g_assert_true(w->autoconnect);
    g_assert_true(w->all_users);
    g_assert_cmpint(w->security, ==, NP_SEC_WPA_PSK);
    g_assert_cmpstr(w->ip4.method, ==, "auto");

    g_autoptr(NpConnModel) wg = np_conn_model_new(NM_SETTING_WIREGUARD_SETTING_NAME);
    g_assert_cmpstr(wg->interface_name, ==, "wg0");
    g_assert_cmpstr(wg->ip4.method, ==, "manual");
    g_assert_cmpstr(wg->ip6.method, ==, "disabled");
    g_assert_cmpuint(wg->wg_peers->len, ==, 1);
    g_assert_false(wg->autoconnect);
}

static void test_general(void)
{
    g_autoptr(NpConnModel) m = wifi_model("net", NP_SEC_NONE, NULL);
    np_set_str(&m->id, "  My Net  ");
    m->autoconnect = FALSE;
    m->autoconnect_priority = 10;
    m->metered = NM_METERED_YES;
    m->all_users = FALSE;
    np_set_str(&m->interface_name, "wlan0");
    NMConnection *c = create_ok(m);
    NMSettingConnection *sc = nm_connection_get_setting_connection(c);
    g_assert_cmpstr(nm_setting_connection_get_id(sc), ==, "My Net");
    g_assert_false(nm_setting_connection_get_autoconnect(sc));
    g_assert_cmpint(nm_setting_connection_get_autoconnect_priority(sc), ==, 10);
    g_assert_cmpint(nm_setting_connection_get_metered(sc), ==, NM_METERED_YES);
    g_assert_cmpuint(nm_setting_connection_get_num_permissions(sc), ==, 1);
    g_assert_cmpstr(nm_setting_connection_get_interface_name(sc), ==, "wlan0");
    g_assert_true(g_uuid_string_is_valid(nm_setting_connection_get_uuid(sc)));

    /* 改回“所有用户”应清空权限 */
    m->all_users = TRUE;
    g_assert_true(np_conn_model_apply(m, c, NULL));
    g_assert_cmpuint(nm_setting_connection_get_num_permissions(sc), ==, 0);
    g_object_unref(c);

    np_set_str(&m->id, "   ");
    assert_create_fails(m);
}

/* ── Wi-Fi ── */

static void test_wifi_psk(void)
{
    g_autoptr(NpConnModel) m = wifi_model("home", NP_SEC_WPA_PSK, "correct horse");
    NMConnection *c = create_ok(m);
    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    char *ssid = np_ssid_to_display(nm_setting_wireless_get_ssid(sw));
    g_assert_cmpstr(ssid, ==, "home");
    g_free(ssid);
    g_assert_cmpstr(nm_setting_wireless_get_mode(sw), ==, NM_SETTING_WIRELESS_MODE_INFRA);
    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
    g_assert_cmpstr(nm_setting_wireless_security_get_key_mgmt(ws), ==, "wpa-psk");
    g_assert_cmpstr(nm_setting_wireless_security_get_psk(ws), ==, "correct horse");
    g_assert_null(nm_connection_get_setting_802_1x(c));
    g_assert_cmpint(np_conn_security_type(c), ==, NP_SEC_WPA_PSK);
    g_object_unref(c);

    np_set_str(&m->psk, "short");
    assert_create_fails(m);
}

static void test_wifi_sae_owe_open(void)
{
    g_autoptr(NpConnModel) sae = wifi_model("w3", NP_SEC_SAE, "abc");
    NMConnection *c = create_ok(sae);
    g_assert_cmpstr(nm_setting_wireless_security_get_key_mgmt(nm_connection_get_setting_wireless_security(c)), ==, "sae");
    g_object_unref(c);

    g_autoptr(NpConnModel) owe = wifi_model("owe", NP_SEC_OWE, NULL);
    c = create_ok(owe);
    g_assert_cmpint(np_conn_security_type(c), ==, NP_SEC_OWE);
    g_object_unref(c);

    g_autoptr(NpConnModel) open = wifi_model("open", NP_SEC_NONE, "ignored");
    c = create_ok(open);
    g_assert_null(nm_connection_get_setting_wireless_security(c));
    g_assert_cmpint(np_conn_security_type(c), ==, NP_SEC_NONE);
    g_object_unref(c);
}

static void test_wifi_options(void)
{
    g_autoptr(NpConnModel) m = wifi_model("x", NP_SEC_NONE, NULL);
    np_set_str(&m->band, "a");
    m->hidden = TRUE;
    np_set_str(&m->cloned_mac, "random");
    m->mtu = 1400;
    NMConnection *c = create_ok(m);
    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    g_assert_cmpstr(nm_setting_wireless_get_band(sw), ==, "a");
    g_assert_true(nm_setting_wireless_get_hidden(sw));
    g_assert_cmpstr(nm_setting_wireless_get_cloned_mac_address(sw), ==, "random");
    g_assert_cmpuint(nm_setting_wireless_get_mtu(sw), ==, 1400);
    g_object_unref(c);

    np_set_str(&m->ssid, "");
    assert_create_fails(m);
}

static void test_wifi_wep(void)
{
    g_autoptr(NpConnModel) m = wifi_model("old", NP_SEC_WEP, NULL);
    np_set_str(&m->wep_key, "0123456789");
    NMConnection *c = create_ok(m);
    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
    g_assert_cmpstr(nm_setting_wireless_security_get_key_mgmt(ws), ==, "none");
    g_assert_cmpint(nm_setting_wireless_security_get_wep_key_type(ws), ==, NM_WEP_KEY_TYPE_KEY);
    g_assert_cmpstr(nm_setting_wireless_security_get_wep_key(ws, 0), ==, "0123456789");
    g_assert_cmpint(np_conn_security_type(c), ==, NP_SEC_WEP);
    g_object_unref(c);

    np_set_str(&m->wep_key, "a passphrase");
    c = create_ok(m);
    g_assert_cmpint(nm_setting_wireless_security_get_wep_key_type(nm_connection_get_setting_wireless_security(c)), ==,
                    NM_WEP_KEY_TYPE_PASSPHRASE);
    g_object_unref(c);
}

static void test_wifi_eap_peap(void)
{
    char *ca = data_path("ca.pem");
    g_autoptr(NpConnModel) m = wifi_model("corp", NP_SEC_EAP, NULL);
    m->eap = NP_EAP_PEAP;
    np_set_str(&m->identity, "alice");
    np_set_str(&m->anonymous_identity, "anon");
    np_set_str(&m->eap_password, "s3cret");
    np_set_str(&m->phase2, "mschapv2");
    np_set_str(&m->ca_cert, ca);
    np_set_str(&m->domain_suffix_match, "corp.example.com");
    NMConnection *c = create_ok(m);
    NMSetting8021x *x = nm_connection_get_setting_802_1x(c);
    g_assert_nonnull(x);
    g_assert_cmpstr(nm_setting_802_1x_get_eap_method(x, 0), ==, "peap");
    g_assert_cmpstr(nm_setting_802_1x_get_identity(x), ==, "alice");
    g_assert_cmpstr(nm_setting_802_1x_get_anonymous_identity(x), ==, "anon");
    g_assert_cmpstr(nm_setting_802_1x_get_password(x), ==, "s3cret");
    g_assert_cmpstr(nm_setting_802_1x_get_phase2_auth(x), ==, "mschapv2");
    g_assert_cmpstr(nm_setting_802_1x_get_ca_cert_path(x), ==, ca);
    g_assert_cmpstr(nm_setting_802_1x_get_domain_suffix_match(x), ==, "corp.example.com");
    g_assert_cmpstr(nm_setting_wireless_security_get_key_mgmt(nm_connection_get_setting_wireless_security(c)), ==, "wpa-eap");

    /* 往返：模型读取应还原全部字段 */
    g_autoptr(NpConnModel) back = np_conn_model_from_connection(c);
    g_assert_cmpint(back->security, ==, NP_SEC_EAP);
    g_assert_cmpint(back->eap, ==, NP_EAP_PEAP);
    g_assert_cmpstr(back->identity, ==, "alice");
    g_assert_cmpstr(back->ca_cert, ==, ca);
    g_object_unref(c);

    np_set_str(&m->identity, "");
    assert_create_fails(m);
    np_set_str(&m->identity, "alice");
    np_set_str(&m->ca_cert, "/nonexistent/ca.pem");
    assert_create_fails(m);
    g_free(ca);
}

static void test_wifi_eap_tls(void)
{
    char *ca = data_path("ca.pem"), *cert = data_path("client.pem"), *key = data_path("client.key");
    g_autoptr(NpConnModel) m = wifi_model("corp", NP_SEC_EAP, NULL);
    m->eap = NP_EAP_TLS;
    np_set_str(&m->identity, "bob");
    np_set_str(&m->ca_cert, ca);
    np_set_str(&m->client_cert, cert);
    np_set_str(&m->private_key, key);
    NMConnection *c = create_ok(m);
    NMSetting8021x *x = nm_connection_get_setting_802_1x(c);
    g_assert_cmpstr(nm_setting_802_1x_get_eap_method(x, 0), ==, "tls");
    g_assert_cmpstr(nm_setting_802_1x_get_client_cert_path(x), ==, cert);
    g_assert_cmpstr(nm_setting_802_1x_get_private_key_path(x), ==, key);
    g_assert_null(nm_setting_802_1x_get_phase2_auth(x));
    g_object_unref(c);

    np_set_str(&m->client_cert, NULL);
    assert_create_fails(m);
    g_free(ca);
    g_free(cert);
    g_free(key);
}

static void test_security_switch(void)
{
    char *ca = data_path("ca.pem");
    g_autoptr(NpConnModel) m = wifi_model("corp", NP_SEC_EAP, NULL);
    np_set_str(&m->identity, "alice");
    np_set_str(&m->eap_password, "pw");
    np_set_str(&m->ca_cert, ca);
    NMConnection *c = create_ok(m);
    g_assert_nonnull(nm_connection_get_setting_802_1x(c));

    m->security = NP_SEC_WPA_PSK;
    np_set_str(&m->psk, "newpassword");
    g_assert_true(np_conn_model_apply(m, c, NULL));
    g_assert_null(nm_connection_get_setting_802_1x(c));
    g_assert_cmpstr(nm_setting_wireless_security_get_psk(nm_connection_get_setting_wireless_security(c)), ==, "newpassword");

    m->security = NP_SEC_NONE;
    g_assert_true(np_conn_model_apply(m, c, NULL));
    g_assert_null(nm_connection_get_setting_wireless_security(c));
    g_object_unref(c);
    g_free(ca);
}

static void test_secret_preserved(void)
{
    /* 密钥字段留空表示“保持不变” */
    g_autoptr(NpConnModel) m = wifi_model("home", NP_SEC_WPA_PSK, "original-pass");
    NMConnection *c = create_ok(m);
    np_set_str(&m->psk, NULL);
    m->autoconnect = FALSE;
    g_assert_true(np_conn_model_apply(m, c, NULL));
    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
    g_assert_cmpstr(nm_setting_wireless_security_get_psk(ws), ==, "original-pass");
    g_assert_false(nm_setting_connection_get_autoconnect(nm_connection_get_setting_connection(c)));
    g_object_unref(c);
}

static void test_hotspot(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new_hotspot("my-hotspot", "hotspot-pass");
    NMConnection *c = create_ok(m);
    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    g_assert_cmpstr(nm_setting_wireless_get_mode(sw), ==, NM_SETTING_WIRELESS_MODE_AP);
    g_assert_cmpstr(nm_setting_wireless_get_band(sw), ==, "bg");
    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
    g_assert_cmpuint(nm_setting_wireless_security_get_num_protos(ws), ==, 1);
    g_assert_cmpstr(nm_setting_wireless_security_get_proto(ws, 0), ==, "rsn");
    g_assert_cmpstr(nm_setting_wireless_security_get_pairwise(ws, 0), ==, "ccmp");
    g_assert_cmpstr(nm_setting_ip_config_get_method(nm_connection_get_setting_ip4_config(c)), ==, "shared");
    g_assert_cmpstr(nm_setting_ip_config_get_method(nm_connection_get_setting_ip6_config(c)), ==, "ignore");
    g_assert_false(nm_setting_connection_get_autoconnect(nm_connection_get_setting_connection(c)));
    g_object_unref(c);

    g_autoptr(NpConnModel) nopass = np_conn_model_new_hotspot("h", NULL);
    assert_create_fails(nopass);
}

/* ── IP ── */

static void test_ip_manual(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
    np_set_str(&m->ip4.method, "manual");
    np_set_str(&m->ip4.addresses, "192.168.1.10/24\n10.0.0.5");
    np_set_str(&m->ip4.gateway, "192.168.1.1");
    np_set_str(&m->ip4.dns, "1.1.1.1, 8.8.8.8");
    np_set_str(&m->ip4.dns_search, "example.com lan");
    np_set_str(&m->ip4.routes, "10.10.0.0/16 via 192.168.1.254 metric 50\n# comment\n172.16.0.0/12");
    np_set_str(&m->ip6.method, "manual");
    np_set_str(&m->ip6.addresses, "fd00::10/64");
    np_set_str(&m->ip6.dns, "2606:4700:4700::1111");
    m->ip6.ip6_privacy = 2;
    NMConnection *c = create_ok(m);

    NMSettingIPConfig *ip4 = nm_connection_get_setting_ip4_config(c);
    g_assert_cmpstr(nm_setting_ip_config_get_method(ip4), ==, "manual");
    g_assert_cmpuint(nm_setting_ip_config_get_num_addresses(ip4), ==, 2);
    g_assert_cmpuint(nm_ip_address_get_prefix(nm_setting_ip_config_get_address(ip4, 1)), ==, 24);
    g_assert_cmpstr(nm_setting_ip_config_get_gateway(ip4), ==, "192.168.1.1");
    g_assert_cmpuint(nm_setting_ip_config_get_num_dns(ip4), ==, 2);
    g_assert_cmpuint(nm_setting_ip_config_get_num_dns_searches(ip4), ==, 2);
    g_assert_cmpuint(nm_setting_ip_config_get_num_routes(ip4), ==, 2);
    g_assert_cmpint(nm_ip_route_get_metric(nm_setting_ip_config_get_route(ip4, 0)), ==, 50);
    g_assert_false(nm_setting_ip_config_get_never_default(ip4));

    NMSettingIPConfig *ip6 = nm_connection_get_setting_ip6_config(c);
    g_assert_cmpuint(nm_setting_ip_config_get_num_addresses(ip6), ==, 1);
    g_assert_cmpint(nm_setting_ip6_config_get_ip6_privacy(NM_SETTING_IP6_CONFIG(ip6)), ==, 2);

    /* 往返一致：从连接读出的模型再写回，应得到完全相同的连接 */
    g_autoptr(NpConnModel) back = np_conn_model_from_connection(c);
    g_assert_cmpstr(back->ip4.dns, ==, "1.1.1.1, 8.8.8.8");
    g_assert_cmpstr(back->ip4.addresses, ==, "192.168.1.10/24\n10.0.0.5/24");
    g_assert_cmpstr(back->ip4.dns_search, ==, "example.com, lan");
    NMConnection *c2 = nm_simple_connection_new_clone(c);
    g_assert_true(np_conn_model_apply(back, c2, NULL));
    g_assert_true(nm_connection_compare(c, c2, NM_SETTING_COMPARE_FLAG_EXACT));
    g_object_unref(c2);
    g_object_unref(c);
}

static void test_ip_route_roundtrip(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
    np_set_str(&m->ip4.routes, "10.10.0.0/16 via 192.168.1.254 metric 50\n172.16.0.0/12");
    NMConnection *c = create_ok(m);
    g_autoptr(NpConnModel) back = np_conn_model_from_connection(c);
    g_assert_cmpstr(back->ip4.routes, ==, "10.10.0.0/16 via 192.168.1.254 metric 50\n172.16.0.0/12");
    g_object_unref(c);
}

static void test_ip_errors(void)
{
    struct { const char *method, *addr, *gw, *dns, *routes; } cases[] = {
        { "manual", "", NULL, NULL, NULL },               /* 手动但无地址 */
        { "manual", "192.168.1.300/24", NULL, NULL, NULL },
        { "manual", "192.168.1.2/24", "nope", NULL, NULL },
        { "auto", "", "192.168.1.1", NULL, NULL },        /* 有网关无地址 */
        { "auto", NULL, NULL, "2606:4700::1111", NULL },  /* IPv4 设置里填了 IPv6 DNS */
        { "auto", NULL, NULL, NULL, "garbage route" },
        { "bogus", NULL, NULL, NULL, NULL },
        { "never-default+gw", "192.168.1.2/24", "192.168.1.1", NULL, NULL },
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
        gboolean nd = !g_strcmp0(cases[i].method, "never-default+gw");
        np_set_str(&m->ip4.method, nd ? "manual" : cases[i].method);
        m->ip4.never_default = nd;
        np_set_str(&m->ip4.addresses, cases[i].addr);
        np_set_str(&m->ip4.gateway, cases[i].gw);
        np_set_str(&m->ip4.dns, cases[i].dns);
        np_set_str(&m->ip4.routes, cases[i].routes);
        assert_create_fails(m);
    }
}

static void test_ip_never_default(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
    np_set_str(&m->ip4.routes, "10.0.0.0/8");
    m->ip4.never_default = TRUE;
    NMConnection *c = create_ok(m);
    g_assert_true(nm_setting_ip_config_get_never_default(nm_connection_get_setting_ip4_config(c)));
    g_object_unref(c);
}

static void test_ip_auto_dns(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
    np_set_str(&m->ip4.dns, "9.9.9.9");
    m->ip4.ignore_auto_dns = TRUE;
    NMConnection *c = create_ok(m);
    NMSettingIPConfig *ip4 = nm_connection_get_setting_ip4_config(c);
    g_assert_cmpstr(nm_setting_ip_config_get_method(ip4), ==, "auto");
    g_assert_true(nm_setting_ip_config_get_ignore_auto_dns(ip4));
    g_assert_cmpstr(nm_setting_ip_config_get_dns(ip4, 0), ==, "9.9.9.9");

    /* 清空 DNS */
    np_set_str(&m->ip4.dns, "");
    g_assert_true(np_conn_model_apply(m, c, NULL));
    g_assert_cmpuint(nm_setting_ip_config_get_num_dns(ip4), ==, 0);
    g_object_unref(c);
}

/* ── 有线 ── */

static void test_wired(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
    np_set_str(&m->id, "LAN");
    np_set_str(&m->cloned_mac, "00:11:22:33:44:55");
    m->mtu = 9000;
    np_set_str(&m->interface_name, "eno1");
    NMConnection *c = create_ok(m);
    NMSettingWired *we = nm_connection_get_setting_wired(c);
    g_assert_cmpstr(nm_setting_wired_get_cloned_mac_address(we), ==, "00:11:22:33:44:55");
    g_assert_cmpuint(nm_setting_wired_get_mtu(we), ==, 9000);
    g_autoptr(NpConnModel) back = np_conn_model_from_connection(c);
    g_assert_cmpstr(back->cloned_mac, ==, "00:11:22:33:44:55");
    g_assert_cmpuint(back->mtu, ==, 9000);
    g_assert_cmpstr(back->interface_name, ==, "eno1");
    g_object_unref(c);

    np_set_str(&m->cloned_mac, "not-a-mac");
    assert_create_fails(m);
}

/* ── WireGuard ── */

#define WG_PRIV "yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk="
#define WG_PUB  "xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg="
#define WG_PSK  "FpCyhws9cxwWoV4xELtfJvjJN+zQVRPISllRWgeopVE="

static NpConnModel *wg_model(void)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_WIREGUARD_SETTING_NAME);
    np_set_str(&m->wg_private_key, WG_PRIV);
    m->wg_listen_port = 51820;
    np_set_str(&m->ip4.addresses, "10.0.0.2/24");
    NpWgPeerModel *p = m->wg_peers->pdata[0];
    np_set_str(&p->public_key, WG_PUB);
    np_set_str(&p->endpoint, "vpn.example.com:51820");
    np_set_str(&p->allowed_ips, "0.0.0.0/0, ::/0");
    np_set_str(&p->preshared_key, WG_PSK);
    p->keepalive = 25;
    return m;
}

static void test_wireguard(void)
{
    g_autoptr(NpConnModel) m = wg_model();
    NMConnection *c = create_ok(m);
    NMSettingWireGuard *wg = NM_SETTING_WIREGUARD(nm_connection_get_setting(c, NM_TYPE_SETTING_WIREGUARD));
    g_assert_cmpstr(nm_setting_wireguard_get_private_key(wg), ==, WG_PRIV);
    g_assert_cmpuint(nm_setting_wireguard_get_listen_port(wg), ==, 51820);
    g_assert_cmpuint(nm_setting_wireguard_get_peers_len(wg), ==, 1);
    NMWireGuardPeer *p = nm_setting_wireguard_get_peer(wg, 0);
    g_assert_cmpstr(nm_wireguard_peer_get_public_key(p), ==, WG_PUB);
    g_assert_cmpstr(nm_wireguard_peer_get_endpoint(p), ==, "vpn.example.com:51820");
    g_assert_cmpuint(nm_wireguard_peer_get_allowed_ips_len(p), ==, 2);
    g_assert_cmpstr(nm_wireguard_peer_get_preshared_key(p), ==, WG_PSK);
    g_assert_cmpuint(nm_wireguard_peer_get_persistent_keepalive(p), ==, 25);
    g_assert_cmpstr(nm_setting_connection_get_interface_name(nm_connection_get_setting_connection(c)), ==, "wg0");

    /* 预共享密钥留空时沿用旧值；增加第二个 peer */
    g_autoptr(NpConnModel) back = np_conn_model_from_connection(c);
    g_assert_cmpuint(back->wg_peers->len, ==, 1);
    np_set_str(&((NpWgPeerModel *)back->wg_peers->pdata[0])->preshared_key, NULL);
    NpWgPeerModel *p2 = np_wg_peer_model_new();
    np_set_str(&p2->public_key, "HIgo9xNzJMWLKASShiTqIybxZ0U3wGLiUeJ1PKf8ykw=");
    np_set_str(&p2->allowed_ips, "10.9.0.0/16");
    g_ptr_array_add(back->wg_peers, p2);
    g_assert_true(np_conn_model_apply(back, c, NULL));
    g_assert_cmpuint(nm_setting_wireguard_get_peers_len(wg), ==, 2);
    g_assert_cmpstr(nm_wireguard_peer_get_preshared_key(nm_setting_wireguard_get_peer(wg, 0)), ==, WG_PSK);
    g_assert_null(nm_wireguard_peer_get_preshared_key(nm_setting_wireguard_get_peer(wg, 1)));
    g_object_unref(c);
}

static void test_wireguard_errors(void)
{
    {
        g_autoptr(NpConnModel) m = wg_model();
        np_set_str(&m->wg_private_key, "short");
        assert_create_fails(m);
    }
    {
        g_autoptr(NpConnModel) m = wg_model();
        np_set_str(&m->wg_private_key, NULL);
        assert_create_fails(m);
    }
    {
        g_autoptr(NpConnModel) m = wg_model();
        np_set_str(&m->interface_name, "");
        assert_create_fails(m);
    }
    {
        g_autoptr(NpConnModel) m = wg_model();
        np_set_str(&((NpWgPeerModel *)m->wg_peers->pdata[0])->public_key, "bad");
        assert_create_fails(m);
    }
    {
        g_autoptr(NpConnModel) m = wg_model();
        np_set_str(&((NpWgPeerModel *)m->wg_peers->pdata[0])->allowed_ips, "10.0.0.0/99");
        assert_create_fails(m);
    }
    {
        g_autoptr(NpConnModel) m = wg_model();
        np_set_str(&((NpWgPeerModel *)m->wg_peers->pdata[0])->endpoint, "no-port");
        assert_create_fails(m);
    }
}

/* ── VPN ── */

static void test_vpn(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_VPN_SETTING_NAME);
    np_set_str(&m->vpn_service, "org.freedesktop.NetworkManager.openvpn");
    np_set_str(&m->vpn_user, "alice");
    np_set_str(&m->vpn_data, "remote = vpn.example.com\nconnection-type = password\npassword-flags = 1\n");
    np_set_str(&m->vpn_secrets, "password = hunter2");
    NMConnection *c = create_ok(m);
    NMSettingVpn *vpn = nm_connection_get_setting_vpn(c);
    g_assert_cmpstr(nm_setting_vpn_get_service_type(vpn), ==, "org.freedesktop.NetworkManager.openvpn");
    g_assert_cmpstr(nm_setting_vpn_get_user_name(vpn), ==, "alice");
    g_assert_cmpstr(nm_setting_vpn_get_data_item(vpn, "remote"), ==, "vpn.example.com");
    g_assert_cmpstr(nm_setting_vpn_get_secret(vpn, "password"), ==, "hunter2");

    /* data 整体替换；secrets 留空则保持 */
    g_autoptr(NpConnModel) back = np_conn_model_from_connection(c);
    g_assert_cmpstr(back->vpn_data, ==, "connection-type = password\npassword-flags = 1\nremote = vpn.example.com\n");
    np_set_str(&back->vpn_data, "remote = other.example.com\n");
    np_set_str(&back->vpn_secrets, NULL);
    g_assert_true(np_conn_model_apply(back, c, NULL));
    g_assert_cmpuint(nm_setting_vpn_get_num_data_items(vpn), ==, 1);
    g_assert_cmpstr(nm_setting_vpn_get_data_item(vpn, "remote"), ==, "other.example.com");
    g_assert_cmpstr(nm_setting_vpn_get_secret(vpn, "password"), ==, "hunter2");
    g_object_unref(c);

    np_set_str(&m->vpn_data, "malformed");
    assert_create_fails(m);
    np_set_str(&m->vpn_data, NULL);
    np_set_str(&m->vpn_service, NULL);
    assert_create_fails(m);
}

/* ── 代理 ── */

static void test_proxy(void)
{
    g_autoptr(NpConnModel) m = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
    NMConnection *c = create_ok(m);
    /* 未启用时：normalize 只会补一个 method=none 的默认代理设置 */
    NMSettingProxy *dflt = nm_connection_get_setting_proxy(c);
    g_assert_true(!dflt || nm_setting_proxy_get_method(dflt) == NM_SETTING_PROXY_METHOD_NONE);
    g_object_unref(c);

    m->proxy_auto = TRUE;
    np_set_str(&m->pac_url, "http://wpad/wpad.dat");
    c = create_ok(m);
    NMSettingProxy *px = nm_connection_get_setting_proxy(c);
    g_assert_cmpint(nm_setting_proxy_get_method(px), ==, NM_SETTING_PROXY_METHOD_AUTO);
    g_assert_cmpstr(nm_setting_proxy_get_pac_url(px), ==, "http://wpad/wpad.dat");

    m->proxy_auto = FALSE;
    g_assert_true(np_conn_model_apply(m, c, NULL));
    g_assert_cmpint(nm_setting_proxy_get_method(px), ==, NM_SETTING_PROXY_METHOD_NONE);
    g_assert_null(nm_setting_proxy_get_pac_url(px));
    g_object_unref(c);

    m->proxy_auto = TRUE;
    np_set_str(&m->pac_url, "ftp://bad");
    assert_create_fails(m);
}

/* ── 往返（所有类型） ── */

static void test_roundtrip_idempotent(void)
{
    char *ca = data_path("ca.pem");
    NpConnModel *models[5];
    models[0] = wifi_model("home", NP_SEC_WPA_PSK, "password123");
    models[1] = np_conn_model_new_hotspot("hs", "password123");
    models[2] = np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME);
    models[3] = wg_model();
    models[4] = wifi_model("corp", NP_SEC_EAP, NULL);
    np_set_str(&models[4]->identity, "u");
    np_set_str(&models[4]->eap_password, "p");
    np_set_str(&models[4]->ca_cert, ca);
    for (guint i = 0; i < G_N_ELEMENTS(models); i++) {
        NMConnection *c = create_ok(models[i]);
        NpConnModel *back = np_conn_model_from_connection(c);
        NMConnection *c2 = nm_simple_connection_new_clone(c);
        GError *e = NULL;
        g_assert_true(np_conn_model_apply(back, c2, &e));
        g_assert_no_error(e);
        g_assert_true(nm_connection_compare(c, c2, NM_SETTING_COMPARE_FLAG_EXACT));
        g_object_unref(c2);
        g_object_unref(c);
        np_conn_model_free(back);
        np_conn_model_free(models[i]);
    }
    g_free(ca);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/conn-model/choice-index", test_choice_index);
    g_test_add_func("/conn-model/defaults", test_defaults);
    g_test_add_func("/conn-model/general", test_general);
    g_test_add_func("/conn-model/wifi/psk", test_wifi_psk);
    g_test_add_func("/conn-model/wifi/sae-owe-open", test_wifi_sae_owe_open);
    g_test_add_func("/conn-model/wifi/options", test_wifi_options);
    g_test_add_func("/conn-model/wifi/wep", test_wifi_wep);
    g_test_add_func("/conn-model/wifi/eap-peap", test_wifi_eap_peap);
    g_test_add_func("/conn-model/wifi/eap-tls", test_wifi_eap_tls);
    g_test_add_func("/conn-model/wifi/security-switch", test_security_switch);
    g_test_add_func("/conn-model/wifi/secret-preserved", test_secret_preserved);
    g_test_add_func("/conn-model/wifi/hotspot", test_hotspot);
    g_test_add_func("/conn-model/ip/manual", test_ip_manual);
    g_test_add_func("/conn-model/ip/route-roundtrip", test_ip_route_roundtrip);
    g_test_add_func("/conn-model/ip/errors", test_ip_errors);
    g_test_add_func("/conn-model/ip/never-default", test_ip_never_default);
    g_test_add_func("/conn-model/ip/auto-dns", test_ip_auto_dns);
    g_test_add_func("/conn-model/wired", test_wired);
    g_test_add_func("/conn-model/wireguard", test_wireguard);
    g_test_add_func("/conn-model/wireguard/errors", test_wireguard_errors);
    g_test_add_func("/conn-model/vpn", test_vpn);
    g_test_add_func("/conn-model/proxy", test_proxy);
    g_test_add_func("/conn-model/roundtrip", test_roundtrip_idempotent);
    return g_test_run();
}
