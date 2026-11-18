#include "np-wg.h"

#include <stdio.h>
#include <string.h>

static void hex(const char *s, uint8_t out[32])
{
    for (int i = 0; i < 32; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

/* RFC 7748 §5.2 测试向量 */
static void test_x25519_vector(void)
{
    uint8_t k[32], u[32], out[32], expect[32];
    hex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k);
    hex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u);
    hex("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", expect);
    np_x25519(out, k, u);
    g_assert_cmpmem(out, 32, expect, 32);
}

/* RFC 7748 §6.1 Diffie-Hellman 向量 */
static void test_x25519_dh(void)
{
    uint8_t a[32], b[32], apub[32], bpub[32], e_apub[32], e_bpub[32], k1[32], k2[32], shared[32];
    hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a);
    hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b);
    hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", e_apub);
    hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", e_bpub);
    hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", shared);
    np_x25519_base(apub, a);
    np_x25519_base(bpub, b);
    g_assert_cmpmem(apub, 32, e_apub, 32);
    g_assert_cmpmem(bpub, 32, e_bpub, 32);
    np_x25519(k1, a, bpub);
    np_x25519(k2, b, apub);
    g_assert_cmpmem(k1, 32, shared, 32);
    g_assert_cmpmem(k2, 32, shared, 32);
}

static void test_public_key(void)
{
    /* 与 `wg pubkey` 的结果一致 */
    char *pub = np_wg_public_key("yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=");
    g_assert_cmpstr(pub, ==, "HIgo9xNzJMWLKASShiTqIybxZ0U3wGLiUeJ1PKf8ykw=");
    g_free(pub);
    g_assert_null(np_wg_public_key("not base64"));
    g_assert_null(np_wg_public_key(NULL));
    g_assert_null(np_wg_public_key("AAAA"));
}

static void test_generate(void)
{
    char *a = np_wg_generate_private_key();
    char *b = np_wg_generate_private_key();
    g_assert_true(np_wg_key_valid(a));
    g_assert_cmpstr(a, !=, b);
    gsize len;
    guint8 *raw = g_base64_decode(a, &len);
    g_assert_cmpuint(len, ==, 32);
    g_assert_cmpuint(raw[0] & 7, ==, 0);    /* 钳位 */
    g_assert_cmpuint(raw[31] & 128, ==, 0);
    g_assert_cmpuint(raw[31] & 64, ==, 64);
    char *pub = np_wg_public_key(a);
    g_assert_true(np_wg_key_valid(pub));
    g_free(pub);
    g_free(raw);
    g_free(a);
    g_free(b);
    g_assert_false(np_wg_key_valid("short"));
    g_assert_false(np_wg_key_valid(NULL));
}

static void test_import(void)
{
    char *path = g_test_build_filename(G_TEST_DIST, "data", "wg0.conf", NULL);
    GError *e = NULL;
    NMConnection *c = np_wg_import(path, &e);
    g_assert_no_error(e);
    g_assert_nonnull(c);
    g_assert_cmpstr(nm_connection_get_connection_type(c), ==, NM_SETTING_WIREGUARD_SETTING_NAME);
    g_assert_cmpstr(nm_connection_get_interface_name(c), ==, "wg0");
    NMSettingWireGuard *wg = NM_SETTING_WIREGUARD(nm_connection_get_setting(c, NM_TYPE_SETTING_WIREGUARD));
    g_assert_cmpuint(nm_setting_wireguard_get_listen_port(wg), ==, 51820);
    g_assert_cmpuint(nm_setting_wireguard_get_peers_len(wg), ==, 1);
    NMWireGuardPeer *p = nm_setting_wireguard_get_peer(wg, 0);
    g_assert_cmpstr(nm_wireguard_peer_get_endpoint(p), ==, "vpn.example.com:51820");
    g_assert_cmpuint(nm_wireguard_peer_get_persistent_keepalive(p), ==, 25);
    NMSettingIPConfig *ip4 = nm_connection_get_setting_ip4_config(c);
    g_assert_cmpuint(nm_setting_ip_config_get_num_addresses(ip4), ==, 1);
    g_object_unref(c);
    g_free(path);

    g_assert_null(np_wg_import("/nonexistent/wg9.conf", &e));
    g_assert_nonnull(e);
    g_clear_error(&e);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/wg/x25519-vector", test_x25519_vector);
    g_test_add_func("/wg/x25519-dh", test_x25519_dh);
    g_test_add_func("/wg/public-key", test_public_key);
    g_test_add_func("/wg/generate", test_generate);
    g_test_add_func("/wg/import", test_import);
    return g_test_run();
}
