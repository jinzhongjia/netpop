// SPDX-License-Identifier: GPL-3.0-or-later
#include "np-util.h"

#include <sys/socket.h>

static void test_band(void)
{
    g_assert_cmpint(np_band_from_frequency(2412), ==, NP_BAND_2GHZ);
    g_assert_cmpint(np_band_from_frequency(2484), ==, NP_BAND_2GHZ);
    g_assert_cmpint(np_band_from_frequency(5180), ==, NP_BAND_5GHZ);
    g_assert_cmpint(np_band_from_frequency(5885), ==, NP_BAND_5GHZ);
    g_assert_cmpint(np_band_from_frequency(5955), ==, NP_BAND_6GHZ);
    g_assert_cmpstr(np_band_label(NP_BAND_2GHZ), ==, "2.4 GHz");
    g_assert_cmpstr(np_band_label(NP_BAND_5GHZ), ==, "5 GHz");
    g_assert_cmpstr(np_band_label(NP_BAND_6GHZ), ==, "6 GHz");
}

static void test_signal(void)
{
    g_assert_cmpuint(np_signal_level(0), ==, 0);
    g_assert_cmpuint(np_signal_level(1), ==, 1);
    g_assert_cmpuint(np_signal_level(24), ==, 1);
    g_assert_cmpuint(np_signal_level(25), ==, 2);
    g_assert_cmpuint(np_signal_level(50), ==, 3);
    g_assert_cmpuint(np_signal_level(75), ==, 4);
    g_assert_cmpuint(np_signal_level(100), ==, 4);
}

static void test_ssid(void)
{
    GError *e = NULL;
    GBytes *b = np_ssid_from_text("咖啡店 WiFi", &e);
    g_assert_no_error(e);
    char *s = np_ssid_to_display(b);
    g_assert_cmpstr(s, ==, "咖啡店 WiFi");
    g_free(s);
    g_bytes_unref(b);

    g_assert_null(np_ssid_from_text("", &e));
    g_assert_error(e, NP_ERROR, NP_ERROR_INVALID);
    g_clear_error(&e);
    g_assert_null(np_ssid_from_text("0123456789012345678901234567890123", &e));
    g_assert_error(e, NP_ERROR, NP_ERROR_INVALID);
    g_clear_error(&e);
    b = np_ssid_from_text("01234567890123456789012345678901", &e); /* 正好 32 字节 */
    g_assert_nonnull(b);
    g_bytes_unref(b);

    s = np_ssid_to_display(NULL);
    g_assert_cmpstr(s, ==, "");
    g_free(s);
}

static void test_security(void)
{
    const guint32 P = NM_802_11_AP_FLAGS_PRIVACY;
    const guint32 PSK = NM_802_11_AP_SEC_KEY_MGMT_PSK, SAE = NM_802_11_AP_SEC_KEY_MGMT_SAE;
    const guint32 EAP = NM_802_11_AP_SEC_KEY_MGMT_802_1X, OWE = NM_802_11_AP_SEC_KEY_MGMT_OWE;

    g_assert_cmpint(np_ap_security_classify(0, 0, 0), ==, NP_AP_SEC_OPEN);
    g_assert_cmpint(np_ap_security_classify(P, 0, 0), ==, NP_AP_SEC_WEP);
    g_assert_cmpint(np_ap_security_classify(P, PSK, 0), ==, NP_AP_SEC_WPA);
    g_assert_cmpint(np_ap_security_classify(P, PSK, PSK), ==, NP_AP_SEC_WPA2);
    g_assert_cmpint(np_ap_security_classify(P, 0, PSK), ==, NP_AP_SEC_WPA2);
    g_assert_cmpint(np_ap_security_classify(P, 0, PSK | SAE), ==, NP_AP_SEC_WPA2_WPA3);
    g_assert_cmpint(np_ap_security_classify(P, 0, SAE), ==, NP_AP_SEC_WPA3);
    g_assert_cmpint(np_ap_security_classify(P, 0, EAP), ==, NP_AP_SEC_ENTERPRISE);
    g_assert_cmpint(np_ap_security_classify(P, EAP, 0), ==, NP_AP_SEC_ENTERPRISE);
    g_assert_cmpint(np_ap_security_classify(0, 0, OWE), ==, NP_AP_SEC_OWE);

    g_assert_false(np_ap_security_is_secure(NP_AP_SEC_OPEN));
    g_assert_false(np_ap_security_is_secure(NP_AP_SEC_OWE));
    g_assert_true(np_ap_security_is_secure(NP_AP_SEC_WPA2));

    g_assert_cmpint(np_ap_security_quick_connect(NP_AP_SEC_OPEN), ==, NP_QUICK_OPEN);
    g_assert_cmpint(np_ap_security_quick_connect(NP_AP_SEC_WPA3), ==, NP_QUICK_PSK);
    g_assert_cmpint(np_ap_security_quick_connect(NP_AP_SEC_ENTERPRISE), ==, NP_QUICK_EDITOR);
    g_assert_cmpint(np_ap_security_quick_connect(NP_AP_SEC_WEP), ==, NP_QUICK_EDITOR);

    g_assert_cmpstr(np_ap_security_key_mgmt(NP_AP_SEC_WPA2), ==, "wpa-psk");
    g_assert_cmpstr(np_ap_security_key_mgmt(NP_AP_SEC_WPA2_WPA3), ==, "wpa-psk");
    g_assert_cmpstr(np_ap_security_key_mgmt(NP_AP_SEC_WPA3), ==, "sae");
    g_assert_null(np_ap_security_key_mgmt(NP_AP_SEC_OPEN));
    g_assert_cmpstr(np_ap_security_label(NP_AP_SEC_ENTERPRISE), ==, "企业级");
}

static void test_psk(void)
{
    GError *e = NULL;
    g_assert_true(np_psk_validate("wpa-psk", "12345678", &e));
    g_assert_true(np_psk_validate("wpa-psk", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", &e));
    g_assert_no_error(e);

    g_assert_false(np_psk_validate("wpa-psk", "1234567", &e));
    g_assert_error(e, NP_ERROR, NP_ERROR_INVALID);
    g_clear_error(&e);
    g_assert_false(np_psk_validate("wpa-psk", "zz23456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", &e));
    g_clear_error(&e);
    g_assert_false(np_psk_validate("wpa-psk", "", &e));
    g_clear_error(&e);
    g_assert_false(np_psk_validate("wpa-psk", NULL, &e));
    g_clear_error(&e);

    /* SAE 允许任意长度 */
    g_assert_true(np_psk_validate("sae", "abc", &e));
    g_assert_no_error(e);
}

static void test_wep_guess(void)
{
    g_assert_cmpint(np_wep_key_type_guess("0123456789"), ==, NM_WEP_KEY_TYPE_KEY);
    g_assert_cmpint(np_wep_key_type_guess("0123456789abcdef0123456789"), ==, NM_WEP_KEY_TYPE_KEY);
    g_assert_cmpint(np_wep_key_type_guess("abcde"), ==, NM_WEP_KEY_TYPE_KEY);
    g_assert_cmpint(np_wep_key_type_guess("abcdefghijklm"), ==, NM_WEP_KEY_TYPE_KEY);
    g_assert_cmpint(np_wep_key_type_guess("some passphrase"), ==, NM_WEP_KEY_TYPE_PASSPHRASE);
}

static void test_split(void)
{
    char **v = np_split_list(" 1.1.1.1,8.8.8.8 ;  9.9.9.9\n\n");
    g_assert_cmpuint(g_strv_length(v), ==, 3);
    g_assert_cmpstr(v[0], ==, "1.1.1.1");
    g_assert_cmpstr(v[2], ==, "9.9.9.9");
    g_strfreev(v);

    v = np_split_list(NULL);
    g_assert_cmpuint(g_strv_length(v), ==, 0);
    g_strfreev(v);

    v = np_split_lines("  a b \n# comment\n\n c\r\n");
    g_assert_cmpuint(g_strv_length(v), ==, 2);
    g_assert_cmpstr(v[0], ==, "a b");
    g_assert_cmpstr(v[1], ==, "c");
    g_strfreev(v);
}

static void test_prefixed(void)
{
    GError *e = NULL;
    char *a = NULL;
    guint p = 0;
    g_assert_true(np_parse_prefixed(AF_INET, "10.0.0.1/8", 24, 1, &a, &p, &e));
    g_assert_cmpstr(a, ==, "10.0.0.1");
    g_assert_cmpuint(p, ==, 8);
    g_clear_pointer(&a, g_free);

    g_assert_true(np_parse_prefixed(AF_INET, "10.0.0.1", 24, 1, &a, &p, &e));
    g_assert_cmpuint(p, ==, 24);
    g_clear_pointer(&a, g_free);

    g_assert_true(np_parse_prefixed(AF_INET6, "fd00::1/48", 64, 1, &a, &p, &e));
    g_assert_cmpuint(p, ==, 48);
    g_clear_pointer(&a, g_free);

    const char *bad[] = { "10.0.0.1/33", "10.0.0.1/", "10.0.0.1/x", "10.0.0/24", "fd00::1", "", "10.0.0.1/0" };
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_assert_false(np_parse_prefixed(AF_INET, bad[i], 24, 1, &a, &p, &e));
        g_assert_error(e, NP_ERROR, NP_ERROR_INVALID);
        g_clear_error(&e);
    }
    g_assert_null(a);
}

static void test_address(void)
{
    GError *e = NULL;
    NMIPAddress *a = np_ip_address_parse(AF_INET, "192.168.1.5", &e);
    g_assert_no_error(e);
    char *s = np_ip_address_format(a);
    g_assert_cmpstr(s, ==, "192.168.1.5/24");
    g_free(s);
    nm_ip_address_unref(a);

    a = np_ip_address_parse(AF_INET6, "2001:db8::5", &e);
    s = np_ip_address_format(a);
    g_assert_cmpstr(s, ==, "2001:db8::5/64");
    g_free(s);
    nm_ip_address_unref(a);

    g_assert_null(np_ip_address_parse(AF_INET, "300.1.1.1", &e));
    g_clear_error(&e);
}

static void check_route(int family, const char *in, const char *expect)
{
    GError *e = NULL;
    NMIPRoute *r = np_ip_route_parse(family, in, &e);
    g_assert_no_error(e);
    g_assert_nonnull(r);
    char *s = np_ip_route_format(r);
    g_assert_cmpstr(s, ==, expect);
    g_free(s);
    nm_ip_route_unref(r);
}

static void test_route(void)
{
    check_route(AF_INET, "10.1.0.0/16", "10.1.0.0/16");
    check_route(AF_INET, "10.1.0.0/16 via 192.168.1.1", "10.1.0.0/16 via 192.168.1.1");
    check_route(AF_INET, "10.1.0.0/16 192.168.1.1 100", "10.1.0.0/16 via 192.168.1.1 metric 100");
    check_route(AF_INET, "10.1.0.0/16 metric 5", "10.1.0.0/16 metric 5");
    check_route(AF_INET, "0.0.0.0/0 via 10.0.0.1", "0.0.0.0/0 via 10.0.0.1");
    check_route(AF_INET, "1.2.3.4", "1.2.3.4/32");
    check_route(AF_INET6, "2001:db8::/32 via fe80::1", "2001:db8::/32 via fe80::1");

    const char *bad[] = { "", "10.1.0.0/16 via", "10.1.0.0/16 via nope", "10.1.0.0/16 foo",
                          "10.1.0.0/16 1.1.1.1 2.2.2.2", "10.1.0.0/40" };
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        GError *e = NULL;
        g_assert_null(np_ip_route_parse(AF_INET, bad[i], &e));
        g_assert_nonnull(e);
        g_clear_error(&e);
    }
}

static void test_kv(void)
{
    GError *e = NULL;
    GHashTable *t = np_kv_parse("# comment\n remote = vpn.example.com \nport=1194\n\nempty =\n", &e);
    g_assert_no_error(e);
    g_assert_cmpuint(g_hash_table_size(t), ==, 3);
    g_assert_cmpstr(g_hash_table_lookup(t, "remote"), ==, "vpn.example.com");
    g_assert_cmpstr(g_hash_table_lookup(t, "port"), ==, "1194");
    g_assert_cmpstr(g_hash_table_lookup(t, "empty"), ==, "");
    char *s = np_kv_format(t);
    g_assert_cmpstr(s, ==, "empty = \nport = 1194\nremote = vpn.example.com\n");
    g_free(s);
    g_hash_table_unref(t);

    /* 值里可以含 “=” */
    t = np_kv_parse("a = b=c", &e);
    g_assert_cmpstr(g_hash_table_lookup(t, "a"), ==, "b=c");
    g_hash_table_unref(t);

    g_assert_null(np_kv_parse("noequals", &e));
    g_assert_error(e, NP_ERROR, NP_ERROR_INVALID);
    g_clear_error(&e);
    g_assert_null(np_kv_parse(" = value", &e));
    g_clear_error(&e);

    s = np_kv_format(NULL);
    g_assert_cmpstr(s, ==, "");
    g_free(s);
}

static void test_expand_home(void)
{
    char *p = np_expand_home("~/x/y");
    char *expect = g_build_filename(g_get_home_dir(), "x/y", NULL);
    g_assert_cmpstr(p, ==, expect);
    g_free(p);
    g_free(expect);
    p = np_expand_home("/abs");
    g_assert_cmpstr(p, ==, "/abs");
    g_free(p);
    p = np_expand_home("~user/x");
    g_assert_cmpstr(p, ==, "~user/x");
    g_free(p);
    g_assert_null(np_expand_home(NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/util/band", test_band);
    g_test_add_func("/util/signal", test_signal);
    g_test_add_func("/util/ssid", test_ssid);
    g_test_add_func("/util/security", test_security);
    g_test_add_func("/util/psk", test_psk);
    g_test_add_func("/util/wep-guess", test_wep_guess);
    g_test_add_func("/util/split", test_split);
    g_test_add_func("/util/prefixed", test_prefixed);
    g_test_add_func("/util/address", test_address);
    g_test_add_func("/util/route", test_route);
    g_test_add_func("/util/kv", test_kv);
    g_test_add_func("/util/expand-home", test_expand_home);
    return g_test_run();
}
