#include "np-conn-model.h"

#include <string.h>
#include <sys/socket.h>

/* ───────────── 选项表 ───────────── */

const NpChoice np_sec_choices[NP_SEC_N] = {
    [NP_SEC_NONE] = { "none", "无" },
    [NP_SEC_WPA_PSK] = { "wpa-psk", "WPA/WPA2 个人" },
    [NP_SEC_SAE] = { "sae", "WPA3 个人" },
    [NP_SEC_EAP] = { "wpa-eap", "WPA/WPA2/WPA3 企业" },
    [NP_SEC_WEP] = { "wep", "WEP（不安全）" },
    [NP_SEC_OWE] = { "owe", "增强开放 (OWE)" },
};

const NpChoice np_eap_choices[NP_EAP_N] = {
    [NP_EAP_PEAP] = { "peap", "PEAP" },
    [NP_EAP_TTLS] = { "ttls", "TTLS" },
    [NP_EAP_TLS] = { "tls", "TLS（证书）" },
    [NP_EAP_PWD] = { "pwd", "PWD" },
};

const NpChoice np_phase2_choices[NP_PHASE2_N] = {
    { "mschapv2", "MSCHAPv2" }, { "pap", "PAP" }, { "gtc", "GTC" }, { "md5", "MD5" },
};

const NpChoice np_ip4_method_choices[NP_IP4_METHOD_N] = {
    { "auto", "自动 (DHCP)" }, { "manual", "手动" }, { "link-local", "仅本地链路" },
    { "shared", "共享给其他设备" }, { "disabled", "禁用" },
};

const NpChoice np_ip6_method_choices[NP_IP6_METHOD_N] = {
    { "auto", "自动" }, { "dhcp", "仅 DHCPv6" }, { "manual", "手动" },
    { "link-local", "仅本地链路" }, { "ignore", "忽略" }, { "disabled", "禁用" },
};

const NpChoice np_mac_choices[NP_MAC_N] = {
    { NULL, "默认" }, { "preserve", "保持不变" }, { "permanent", "硬件地址" },
    { "random", "每次随机" }, { "stable", "每个网络固定随机" },
};

const NpChoice np_band_choices[NP_BAND_N] = {
    { NULL, "自动" }, { "bg", "2.4 GHz" }, { "a", "5 GHz" },
};

const NpChoice np_metered_choices[NP_METERED_N] = {
    { "unknown", "自动判断" }, { "yes", "是" }, { "no", "否" },
};

const NpChoice np_privacy_choices[NP_PRIVACY_N] = {
    { "-1", "默认" }, { "0", "禁用" }, { "1", "启用（优先公共地址）" }, { "2", "启用（优先临时地址）" },
};

guint np_choice_index(const NpChoice *c, guint n, const char *value)
{
    for (guint i = 0; i < n; i++)
        if (g_strcmp0(c[i].value, value) == 0) return i;
    return n;
}

void np_set_str(char **field, const char *value)
{
    if (*field == value) return;
    char *old = *field;
    *field = g_strdup(value);
    g_free(old);
}

static gboolean empty(const char *s)
{
    return !s || !*s;
}

static const char *or_null(const char *s)
{
    return empty(s) ? NULL : s;
}

/* ───────────── 构造 / 释放 ───────────── */

NpWgPeerModel *np_wg_peer_model_new(void)
{
    NpWgPeerModel *p = g_new0(NpWgPeerModel, 1);
    p->allowed_ips = g_strdup("0.0.0.0/0, ::/0");
    return p;
}

void np_wg_peer_model_free(NpWgPeerModel *p)
{
    if (!p) return;
    g_free(p->public_key);
    g_free(p->endpoint);
    g_free(p->allowed_ips);
    g_free(p->preshared_key);
    g_free(p);
}

static void ip_model_init(NpIpModel *ip, const char *method)
{
    memset(ip, 0, sizeof *ip);
    ip->method = g_strdup(method);
    ip->ip6_privacy = -1;
}

static void ip_model_clear(NpIpModel *ip)
{
    g_free(ip->method);
    g_free(ip->addresses);
    g_free(ip->gateway);
    g_free(ip->dns);
    g_free(ip->dns_search);
    g_free(ip->routes);
}

NpConnModel *np_conn_model_new(const char *type)
{
    NpConnModel *m = g_new0(NpConnModel, 1);
    m->type = g_strdup(type);
    m->autoconnect = TRUE;
    m->all_users = TRUE;
    m->metered = NM_METERED_UNKNOWN;
    m->eap = NP_EAP_PEAP;
    m->phase2 = g_strdup("mschapv2");
    m->wg_peers = g_ptr_array_new_with_free_func((GDestroyNotify)np_wg_peer_model_free);
    ip_model_init(&m->ip4, "auto");
    ip_model_init(&m->ip6, "auto");

    if (!g_strcmp0(type, NM_SETTING_WIRELESS_SETTING_NAME)) {
        m->id = g_strdup("Wi-Fi");
        m->security = NP_SEC_WPA_PSK;
    } else if (!g_strcmp0(type, NM_SETTING_WIRED_SETTING_NAME)) {
        m->id = g_strdup("有线连接");
    } else if (!g_strcmp0(type, NM_SETTING_WIREGUARD_SETTING_NAME)) {
        m->id = g_strdup("WireGuard");
        m->interface_name = g_strdup("wg0");
        m->autoconnect = FALSE;
        np_set_str(&m->ip4.method, "manual");
        np_set_str(&m->ip6.method, "disabled");
        g_ptr_array_add(m->wg_peers, np_wg_peer_model_new());
    } else if (!g_strcmp0(type, NM_SETTING_VPN_SETTING_NAME)) {
        m->id = g_strdup("VPN");
        m->autoconnect = FALSE;
    } else {
        m->id = g_strdup(type);
    }
    return m;
}

NpConnModel *np_conn_model_new_hotspot(const char *ssid, const char *psk)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    np_set_str(&m->id, "个人热点");
    np_set_str(&m->ssid, ssid);
    np_set_str(&m->psk, psk);
    np_set_str(&m->band, "bg");
    np_set_str(&m->ip4.method, "shared");
    np_set_str(&m->ip6.method, "ignore");
    m->ap_mode = TRUE;
    m->autoconnect = FALSE;
    m->security = NP_SEC_WPA_PSK;
    return m;
}

void np_conn_model_free(NpConnModel *m)
{
    if (!m) return;
    char **strs[] = {
        &m->type, &m->uuid, &m->id, &m->interface_name, &m->ssid, &m->band, &m->cloned_mac, &m->psk,
        &m->wep_key, &m->identity, &m->anonymous_identity, &m->eap_password, &m->phase2, &m->ca_cert,
        &m->domain_suffix_match, &m->client_cert, &m->private_key, &m->private_key_password,
        &m->wg_private_key, &m->vpn_service, &m->vpn_user, &m->vpn_data, &m->vpn_secrets, &m->pac_url,
    };
    for (guint i = 0; i < G_N_ELEMENTS(strs); i++) g_free(*strs[i]);
    g_ptr_array_unref(m->wg_peers);
    ip_model_clear(&m->ip4);
    ip_model_clear(&m->ip6);
    g_free(m);
}

/* ───────────── NMConnection → 模型 ───────────── */

NpSecType np_conn_security_type(NMConnection *c)
{
    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
    if (!ws) return NP_SEC_NONE;
    const char *km = nm_setting_wireless_security_get_key_mgmt(ws);
    if (!g_strcmp0(km, "wpa-psk")) return NP_SEC_WPA_PSK;
    if (!g_strcmp0(km, "sae")) return NP_SEC_SAE;
    if (!g_strcmp0(km, "owe")) return NP_SEC_OWE;
    if (!g_strcmp0(km, "none")) return NP_SEC_WEP;
    if (!g_strcmp0(km, "wpa-eap") || !g_strcmp0(km, "wpa-eap-suite-b-192") || !g_strcmp0(km, "ieee8021x"))
        return NP_SEC_EAP;
    return NP_SEC_NONE;
}

static char *join_list(guint n, const char *(*get)(gpointer, guint), gpointer obj, const char *sep)
{
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < n; i++) g_string_append_printf(s, "%s%s", i ? sep : "", get(obj, i));
    return g_string_free(s, FALSE);
}

static const char *get_dns(gpointer s, guint i) { return nm_setting_ip_config_get_dns(s, i); }
static const char *get_search(gpointer s, guint i) { return nm_setting_ip_config_get_dns_search(s, i); }

static void ip_from_setting(NpIpModel *ip, NMSettingIPConfig *s)
{
    if (!s) return;
    np_set_str(&ip->method, nm_setting_ip_config_get_method(s));

    GString *a = g_string_new(NULL);
    for (guint i = 0; i < nm_setting_ip_config_get_num_addresses(s); i++) {
        char *t = np_ip_address_format(nm_setting_ip_config_get_address(s, i));
        g_string_append_printf(a, "%s%s", i ? "\n" : "", t);
        g_free(t);
    }
    g_free(ip->addresses);
    ip->addresses = g_string_free(a, FALSE);

    GString *r = g_string_new(NULL);
    for (guint i = 0; i < nm_setting_ip_config_get_num_routes(s); i++) {
        char *t = np_ip_route_format(nm_setting_ip_config_get_route(s, i));
        g_string_append_printf(r, "%s%s", i ? "\n" : "", t);
        g_free(t);
    }
    g_free(ip->routes);
    ip->routes = g_string_free(r, FALSE);

    np_set_str(&ip->gateway, nm_setting_ip_config_get_gateway(s));
    g_free(ip->dns);
    ip->dns = join_list(nm_setting_ip_config_get_num_dns(s), get_dns, s, ", ");
    g_free(ip->dns_search);
    ip->dns_search = join_list(nm_setting_ip_config_get_num_dns_searches(s), get_search, s, ", ");
    ip->never_default = nm_setting_ip_config_get_never_default(s);
    ip->ignore_auto_dns = nm_setting_ip_config_get_ignore_auto_dns(s);
    if (NM_IS_SETTING_IP6_CONFIG(s))
        ip->ip6_privacy = nm_setting_ip6_config_get_ip6_privacy(NM_SETTING_IP6_CONFIG(s));
}

static void kv_collect(const char *key, const char *value, gpointer table)
{
    g_hash_table_replace(table, g_strdup(key), g_strdup(value ? value : ""));
}

static char *vpn_items(NMSettingVpn *vpn, gboolean secrets)
{
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (secrets) nm_setting_vpn_foreach_secret(vpn, kv_collect, t);
    else nm_setting_vpn_foreach_data_item(vpn, kv_collect, t);
    char *s = np_kv_format(t);
    g_hash_table_unref(t);
    return s;
}

/* libnm 的 get_*_path() 在 scheme 不是 PATH 时会断言失败，必须先检查 */
#define CERT_PATH(x, what)                                                                     \
    (nm_setting_802_1x_get_##what##_scheme(x) == NM_SETTING_802_1X_CK_SCHEME_PATH            \
         ? nm_setting_802_1x_get_##what##_path(x)                                            \
         : NULL)

NpConnModel *np_conn_model_from_connection(NMConnection *c)
{
    const char *type = nm_connection_get_connection_type(c);
    NpConnModel *m = np_conn_model_new(type);
    g_ptr_array_set_size(m->wg_peers, 0);
    np_set_str(&m->interface_name, NULL);
    m->security = np_conn_security_type(c);

    NMSettingConnection *sc = nm_connection_get_setting_connection(c);
    if (sc) {
        np_set_str(&m->uuid, nm_setting_connection_get_uuid(sc));
        np_set_str(&m->id, nm_setting_connection_get_id(sc));
        np_set_str(&m->interface_name, nm_setting_connection_get_interface_name(sc));
        m->autoconnect = nm_setting_connection_get_autoconnect(sc);
        m->autoconnect_priority = nm_setting_connection_get_autoconnect_priority(sc);
        m->metered = nm_setting_connection_get_metered(sc);
        m->all_users = nm_setting_connection_get_num_permissions(sc) == 0;
    }

    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    if (sw) {
        g_free(m->ssid);
        m->ssid = np_ssid_to_display(nm_setting_wireless_get_ssid(sw));
        m->ap_mode = !g_strcmp0(nm_setting_wireless_get_mode(sw), NM_SETTING_WIRELESS_MODE_AP);
        np_set_str(&m->band, nm_setting_wireless_get_band(sw));
        m->hidden = nm_setting_wireless_get_hidden(sw);
        np_set_str(&m->cloned_mac, nm_setting_wireless_get_cloned_mac_address(sw));
        m->mtu = nm_setting_wireless_get_mtu(sw);
    }
    NMSettingWired *we = nm_connection_get_setting_wired(c);
    if (we) {
        np_set_str(&m->cloned_mac, nm_setting_wired_get_cloned_mac_address(we));
        m->mtu = nm_setting_wired_get_mtu(we);
    }

    NMSettingWirelessSecurity *ws = nm_connection_get_setting_wireless_security(c);
    if (ws) {
        np_set_str(&m->psk, nm_setting_wireless_security_get_psk(ws));
        np_set_str(&m->wep_key, nm_setting_wireless_security_get_wep_key(ws, nm_setting_wireless_security_get_wep_tx_keyidx(ws)));
    }
    NMSetting8021x *x = nm_connection_get_setting_802_1x(c);
    if (x) {
        if (nm_setting_802_1x_get_num_eap_methods(x)) {
            guint i = np_choice_index(np_eap_choices, NP_EAP_N, nm_setting_802_1x_get_eap_method(x, 0));
            m->eap = i < NP_EAP_N ? (NpEapMethod)i : NP_EAP_PEAP;
        }
        np_set_str(&m->identity, nm_setting_802_1x_get_identity(x));
        np_set_str(&m->anonymous_identity, nm_setting_802_1x_get_anonymous_identity(x));
        np_set_str(&m->eap_password, nm_setting_802_1x_get_password(x));
        if (nm_setting_802_1x_get_phase2_auth(x)) np_set_str(&m->phase2, nm_setting_802_1x_get_phase2_auth(x));
        np_set_str(&m->ca_cert, CERT_PATH(x, ca_cert));
        m->system_ca_certs = nm_setting_802_1x_get_system_ca_certs(x);
        np_set_str(&m->domain_suffix_match, nm_setting_802_1x_get_domain_suffix_match(x));
        np_set_str(&m->client_cert, CERT_PATH(x, client_cert));
        np_set_str(&m->private_key, CERT_PATH(x, private_key));
        np_set_str(&m->private_key_password, nm_setting_802_1x_get_private_key_password(x));
    }

    NMSettingWireGuard *wg = NM_SETTING_WIREGUARD(nm_connection_get_setting(c, NM_TYPE_SETTING_WIREGUARD));
    if (wg) {
        np_set_str(&m->wg_private_key, nm_setting_wireguard_get_private_key(wg));
        m->wg_listen_port = nm_setting_wireguard_get_listen_port(wg);
        m->mtu = nm_setting_wireguard_get_mtu(wg);
        for (guint i = 0; i < nm_setting_wireguard_get_peers_len(wg); i++) {
            NMWireGuardPeer *p = nm_setting_wireguard_get_peer(wg, i);
            NpWgPeerModel *pm = np_wg_peer_model_new();
            np_set_str(&pm->public_key, nm_wireguard_peer_get_public_key(p));
            np_set_str(&pm->endpoint, nm_wireguard_peer_get_endpoint(p));
            np_set_str(&pm->preshared_key, nm_wireguard_peer_get_preshared_key(p));
            pm->keepalive = nm_wireguard_peer_get_persistent_keepalive(p);
            GString *ips = g_string_new(NULL);
            for (guint j = 0; j < nm_wireguard_peer_get_allowed_ips_len(p); j++)
                g_string_append_printf(ips, "%s%s", j ? ", " : "", nm_wireguard_peer_get_allowed_ip(p, j, NULL));
            g_free(pm->allowed_ips);
            pm->allowed_ips = g_string_free(ips, FALSE);
            g_ptr_array_add(m->wg_peers, pm);
        }
    }

    NMSettingVpn *vpn = nm_connection_get_setting_vpn(c);
    if (vpn) {
        np_set_str(&m->vpn_service, nm_setting_vpn_get_service_type(vpn));
        np_set_str(&m->vpn_user, nm_setting_vpn_get_user_name(vpn));
        g_free(m->vpn_data);
        m->vpn_data = vpn_items(vpn, FALSE);
        g_free(m->vpn_secrets);
        m->vpn_secrets = vpn_items(vpn, TRUE);
    }

    NMSettingProxy *px = nm_connection_get_setting_proxy(c);
    if (px) {
        m->proxy_auto = nm_setting_proxy_get_method(px) == NM_SETTING_PROXY_METHOD_AUTO;
        np_set_str(&m->pac_url, nm_setting_proxy_get_pac_url(px));
    }

    ip_from_setting(&m->ip4, nm_connection_get_setting_ip4_config(c));
    ip_from_setting(&m->ip6, nm_connection_get_setting_ip6_config(c));
    return m;
}

/* ───────────── 模型 → NMConnection ───────────── */

static NMSetting *ensure(NMConnection *c, GType type)
{
    NMSetting *s = nm_connection_get_setting(c, type);
    if (!s) {
        s = g_object_new(type, NULL);
        nm_connection_add_setting(c, s);
    }
    return s;
}

static gboolean fail(GError **error, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static gboolean fail(GError **error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, msg);
    g_free(msg);
    return FALSE;
}

static gboolean apply_connection(const NpConnModel *m, NMConnection *c, GError **error)
{
    NMSettingConnection *sc = NM_SETTING_CONNECTION(ensure(c, NM_TYPE_SETTING_CONNECTION));
    char *id = g_strstrip(g_strdup(m->id ? m->id : ""));
    if (!*id) {
        g_free(id);
        return fail(error, "名称不能为空");
    }
    g_object_set(sc,
                 NM_SETTING_CONNECTION_ID, id,
                 NM_SETTING_CONNECTION_AUTOCONNECT, m->autoconnect,
                 NM_SETTING_CONNECTION_AUTOCONNECT_PRIORITY, m->autoconnect_priority,
                 NM_SETTING_CONNECTION_METERED, m->metered,
                 NM_SETTING_CONNECTION_INTERFACE_NAME, or_null(m->interface_name),
                 NULL);
    g_free(id);
    if (!nm_setting_connection_get_connection_type(sc))
        g_object_set(sc, NM_SETTING_CONNECTION_TYPE, m->type, NULL);

    if (m->all_users) {
        g_object_set(sc, NM_SETTING_CONNECTION_PERMISSIONS, NULL, NULL);
    } else if (nm_setting_connection_get_num_permissions(sc) == 0) {
        nm_setting_connection_add_permission(sc, "user", g_get_user_name(), NULL);
    }
    return TRUE;
}

static gboolean apply_wireless(const NpConnModel *m, NMConnection *c, GError **error)
{
    NMSettingWireless *sw = NM_SETTING_WIRELESS(ensure(c, NM_TYPE_SETTING_WIRELESS));
    GBytes *ssid = np_ssid_from_text(m->ssid, error);
    if (!ssid) return FALSE;
    g_object_set(sw,
                 NM_SETTING_WIRELESS_SSID, ssid,
                 NM_SETTING_WIRELESS_MODE, m->ap_mode ? NM_SETTING_WIRELESS_MODE_AP : NM_SETTING_WIRELESS_MODE_INFRA,
                 NM_SETTING_WIRELESS_BAND, or_null(m->band),
                 NM_SETTING_WIRELESS_HIDDEN, m->hidden,
                 NM_SETTING_WIRELESS_CLONED_MAC_ADDRESS, or_null(m->cloned_mac),
                 NM_SETTING_WIRELESS_MTU, m->mtu,
                 NULL);
    if (!m->band) g_object_set(sw, NM_SETTING_WIRELESS_CHANNEL, 0u, NULL);
    g_bytes_unref(ssid);
    return TRUE;
}

static gboolean apply_8021x(const NpConnModel *m, NMConnection *c, GError **error)
{
    NMSetting8021x *x = NM_SETTING_802_1X(ensure(c, NM_TYPE_SETTING_802_1X));
    if (empty(m->identity)) return fail(error, "请填写身份（用户名）");

    nm_setting_802_1x_clear_eap_methods(x);
    nm_setting_802_1x_add_eap_method(x, np_eap_choices[m->eap].value);
    g_object_set(x,
                 NM_SETTING_802_1X_IDENTITY, m->identity,
                 NM_SETTING_802_1X_ANONYMOUS_IDENTITY, or_null(m->anonymous_identity),
                 NM_SETTING_802_1X_SYSTEM_CA_CERTS, m->system_ca_certs,
                 NM_SETTING_802_1X_DOMAIN_SUFFIX_MATCH, or_null(m->domain_suffix_match),
                 NM_SETTING_802_1X_PHASE2_AUTH,
                 (m->eap == NP_EAP_PEAP || m->eap == NP_EAP_TTLS) ? or_null(m->phase2) : NULL,
                 NULL);

    if (empty(m->ca_cert)) g_object_set(x, NM_SETTING_802_1X_CA_CERT, NULL, NULL);
    else if (!nm_setting_802_1x_set_ca_cert(x, m->ca_cert, NM_SETTING_802_1X_CK_SCHEME_PATH, NULL, error))
        return FALSE;

    if (m->eap == NP_EAP_TLS) {
        if (empty(m->client_cert)) return fail(error, "TLS 需要客户端证书");
        if (empty(m->private_key)) return fail(error, "TLS 需要私钥文件");
        if (!nm_setting_802_1x_set_client_cert(x, m->client_cert, NM_SETTING_802_1X_CK_SCHEME_PATH, NULL, error))
            return FALSE;
        const char *pw = !empty(m->private_key_password) ? m->private_key_password
                                                          : nm_setting_802_1x_get_private_key_password(x);
        char *pwc = g_strdup(pw); /* set_private_key 会覆盖原值 */
        gboolean ok = nm_setting_802_1x_set_private_key(x, m->private_key, pwc,
                                                        NM_SETTING_802_1X_CK_SCHEME_PATH, NULL, error);
        g_free(pwc);
        if (!ok) return FALSE;
        g_object_set(x, NM_SETTING_802_1X_PASSWORD, NULL, NULL);
    } else {
        g_object_set(x, NM_SETTING_802_1X_CLIENT_CERT, NULL, NM_SETTING_802_1X_PRIVATE_KEY, NULL, NULL);
        if (!empty(m->eap_password)) g_object_set(x, NM_SETTING_802_1X_PASSWORD, m->eap_password, NULL);
    }
    return TRUE;
}

static gboolean apply_security(const NpConnModel *m, NMConnection *c, GError **error)
{
    if (m->security != np_conn_security_type(c)) {
        /* 换了加密方式：丢弃旧的安全设置，避免残留的密钥 / 参数 */
        nm_connection_remove_setting(c, NM_TYPE_SETTING_WIRELESS_SECURITY);
        nm_connection_remove_setting(c, NM_TYPE_SETTING_802_1X);
    }
    if (m->security == NP_SEC_NONE) {
        nm_connection_remove_setting(c, NM_TYPE_SETTING_WIRELESS_SECURITY);
        nm_connection_remove_setting(c, NM_TYPE_SETTING_802_1X);
        return TRUE;
    }

    NMSettingWirelessSecurity *ws = NM_SETTING_WIRELESS_SECURITY(ensure(c, NM_TYPE_SETTING_WIRELESS_SECURITY));
    if (m->security != NP_SEC_EAP) nm_connection_remove_setting(c, NM_TYPE_SETTING_802_1X);

    switch (m->security) {
    case NP_SEC_WPA_PSK:
    case NP_SEC_SAE: {
        const char *km = np_sec_choices[m->security].value;
        g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, km, NULL);
        if (!empty(m->psk)) {
            if (!np_psk_validate(km, m->psk, error)) return FALSE;
            g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_PSK, m->psk, NULL);
        } else if (m->ap_mode && !nm_setting_wireless_security_get_psk(ws)) {
            return fail(error, "热点需要设置密码");
        }
        if (m->ap_mode && m->security == NP_SEC_WPA_PSK && !nm_setting_wireless_security_get_num_protos(ws)) {
            /* 与 nm-applet / GNOME 热点相同：只用 WPA2-CCMP，兼容性最好 */
            nm_setting_wireless_security_add_proto(ws, "rsn");
            nm_setting_wireless_security_add_pairwise(ws, "ccmp");
            nm_setting_wireless_security_add_group(ws, "ccmp");
        }
        break;
    }
    case NP_SEC_OWE:
        g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "owe", NULL);
        break;
    case NP_SEC_WEP:
        g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "none",
                     NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open",
                     NM_SETTING_WIRELESS_SECURITY_WEP_TX_KEYIDX, 0u, NULL);
        if (!empty(m->wep_key)) {
            NMWepKeyType t = np_wep_key_type_guess(m->wep_key);
            if (!nm_utils_wep_key_valid(m->wep_key, t)) return fail(error, "WEP 密钥无效");
            g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_WEP_KEY_TYPE, t, NULL);
            nm_setting_wireless_security_set_wep_key(ws, 0, m->wep_key);
        }
        break;
    case NP_SEC_EAP:
        g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-eap", NULL);
        return apply_8021x(m, c, error);
    default:
        break;
    }
    return TRUE;
}

static gboolean apply_wired(const NpConnModel *m, NMConnection *c, GError **error)
{
    NMSettingWired *we = NM_SETTING_WIRED(ensure(c, NM_TYPE_SETTING_WIRED));
    g_object_set(we,
                 NM_SETTING_WIRED_CLONED_MAC_ADDRESS, or_null(m->cloned_mac),
                 NM_SETTING_WIRED_MTU, m->mtu,
                 NULL);
    return TRUE;
}

static gboolean wg_key_ok(const char *k)
{
    guint8 buf[32];
    return k && nm_utils_base64secret_decode(k, sizeof buf, buf);
}

static gboolean apply_wireguard(const NpConnModel *m, NMConnection *c, GError **error)
{
    NMSettingWireGuard *wg = NM_SETTING_WIREGUARD(ensure(c, NM_TYPE_SETTING_WIREGUARD));
    if (empty(m->interface_name)) return fail(error, "WireGuard 需要指定接口名（如 wg0）");
    if (!empty(m->wg_private_key)) {
        if (!wg_key_ok(m->wg_private_key)) return fail(error, "私钥格式无效（应为 44 字符的 Base64）");
        g_object_set(wg, NM_SETTING_WIREGUARD_PRIVATE_KEY, m->wg_private_key, NULL);
    } else if (!nm_setting_wireguard_get_private_key(wg)) {
        return fail(error, "请填写或生成私钥");
    }
    if (m->wg_listen_port > 65535) return fail(error, "监听端口无效");
    g_object_set(wg, NM_SETTING_WIREGUARD_LISTEN_PORT, m->wg_listen_port, NM_SETTING_WIREGUARD_MTU, m->mtu, NULL);

    /* 记下旧 peer 的预共享密钥，未填写时沿用 */
    GHashTable *old_psk = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint i = 0; i < nm_setting_wireguard_get_peers_len(wg); i++) {
        NMWireGuardPeer *p = nm_setting_wireguard_get_peer(wg, i);
        if (nm_wireguard_peer_get_public_key(p) && nm_wireguard_peer_get_preshared_key(p))
            g_hash_table_insert(old_psk, g_strdup(nm_wireguard_peer_get_public_key(p)),
                                g_strdup(nm_wireguard_peer_get_preshared_key(p)));
    }

    gboolean ok = TRUE;
    GPtrArray *peers = g_ptr_array_new_with_free_func((GDestroyNotify)nm_wireguard_peer_unref);
    for (guint i = 0; ok && i < m->wg_peers->len; i++) {
        NpWgPeerModel *pm = m->wg_peers->pdata[i];
        NMWireGuardPeer *p = nm_wireguard_peer_new();
        g_ptr_array_add(peers, p);
        if (!wg_key_ok(pm->public_key) || !nm_wireguard_peer_set_public_key(p, pm->public_key, FALSE)) {
            ok = fail(error, "Peer %u 的公钥无效", i + 1);
            break;
        }
        if (!empty(pm->endpoint) && !nm_wireguard_peer_set_endpoint(p, pm->endpoint, FALSE)) {
            ok = fail(error, "Peer %u 的 Endpoint 无效（格式：主机:端口）", i + 1);
            break;
        }
        char **ips = np_split_list(pm->allowed_ips);
        for (char **ip = ips; *ip; ip++)
            if (!nm_wireguard_peer_append_allowed_ip(p, *ip, FALSE)) {
                ok = fail(error, "Peer %u 的 AllowedIPs 无效：%s", i + 1, *ip);
                break;
            }
        g_strfreev(ips);
        if (!ok) break;
        const char *psk = !empty(pm->preshared_key) ? pm->preshared_key : g_hash_table_lookup(old_psk, pm->public_key);
        if (psk) {
            if (!wg_key_ok(psk) || !nm_wireguard_peer_set_preshared_key(p, psk, FALSE)) {
                ok = fail(error, "Peer %u 的预共享密钥无效", i + 1);
                break;
            }
            nm_wireguard_peer_set_preshared_key_flags(p, NM_SETTING_SECRET_FLAG_NONE);
        }
        if (pm->keepalive > 65535) {
            ok = fail(error, "Peer %u 的保活间隔无效", i + 1);
            break;
        }
        nm_wireguard_peer_set_persistent_keepalive(p, (guint16)pm->keepalive);
        nm_wireguard_peer_seal(p);
    }
    if (ok) {
        nm_setting_wireguard_clear_peers(wg);
        for (guint i = 0; i < peers->len; i++) nm_setting_wireguard_append_peer(wg, peers->pdata[i]);
    }
    g_ptr_array_unref(peers);
    g_hash_table_unref(old_psk);
    return ok;
}

static gboolean apply_vpn(const NpConnModel *m, NMConnection *c, GError **error)
{
    NMSettingVpn *vpn = NM_SETTING_VPN(ensure(c, NM_TYPE_SETTING_VPN));
    if (!empty(m->vpn_service)) g_object_set(vpn, NM_SETTING_VPN_SERVICE_TYPE, m->vpn_service, NULL);
    if (!nm_setting_vpn_get_service_type(vpn)) return fail(error, "VPN 缺少服务类型");
    g_object_set(vpn, NM_SETTING_VPN_USER_NAME, or_null(m->vpn_user), NULL);

    if (m->vpn_data) {
        GHashTable *data = np_kv_parse(m->vpn_data, error);
        if (!data) return FALSE;
        guint n = 0;
        const char **keys = nm_setting_vpn_get_data_keys(vpn, &n);
        char **copy = g_new0(char *, n + 1);
        for (guint i = 0; i < n; i++) copy[i] = g_strdup(keys[i]);
        g_free(keys);
        for (guint i = 0; i < n; i++) nm_setting_vpn_remove_data_item(vpn, copy[i]);
        g_strfreev(copy);
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, data);
        while (g_hash_table_iter_next(&it, &k, &v)) nm_setting_vpn_add_data_item(vpn, k, v);
        g_hash_table_unref(data);
    }
    if (!empty(m->vpn_secrets)) {
        GHashTable *sec = np_kv_parse(m->vpn_secrets, error);
        if (!sec) return FALSE;
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, sec);
        while (g_hash_table_iter_next(&it, &k, &v)) nm_setting_vpn_add_secret(vpn, k, v);
        g_hash_table_unref(sec);
    }
    return TRUE;
}

static gboolean apply_proxy(const NpConnModel *m, NMConnection *c, GError **error)
{
    NMSettingProxy *px = nm_connection_get_setting_proxy(c);
    if (!px && !m->proxy_auto) return TRUE;
    px = NM_SETTING_PROXY(ensure(c, NM_TYPE_SETTING_PROXY));
    if (m->proxy_auto && !empty(m->pac_url) && !g_str_has_prefix(m->pac_url, "http://") &&
        !g_str_has_prefix(m->pac_url, "https://") && !g_str_has_prefix(m->pac_url, "file://"))
        return fail(error, "PAC 地址应以 http://、https:// 或 file:// 开头");
    g_object_set(px,
                 NM_SETTING_PROXY_METHOD, m->proxy_auto ? NM_SETTING_PROXY_METHOD_AUTO : NM_SETTING_PROXY_METHOD_NONE,
                 NM_SETTING_PROXY_PAC_URL, m->proxy_auto ? or_null(m->pac_url) : NULL,
                 NULL);
    return TRUE;
}

static gboolean apply_ip(const NpIpModel *ip, NMConnection *c, int family, GError **error)
{
    const char *fam = family == AF_INET6 ? "IPv6" : "IPv4";
    NMSettingIPConfig *s = family == AF_INET6
                               ? NM_SETTING_IP_CONFIG(ensure(c, NM_TYPE_SETTING_IP6_CONFIG))
                               : NM_SETTING_IP_CONFIG(ensure(c, NM_TYPE_SETTING_IP4_CONFIG));
    const NpChoice *methods = family == AF_INET6 ? np_ip6_method_choices : np_ip4_method_choices;
    guint nm = family == AF_INET6 ? NP_IP6_METHOD_N : NP_IP4_METHOD_N;
    if (np_choice_index(methods, nm, ip->method) >= nm) return fail(error, "%s 方式无效：%s", fam, ip->method);

    GPtrArray *addrs = g_ptr_array_new_with_free_func((GDestroyNotify)nm_ip_address_unref);
    GPtrArray *routes = g_ptr_array_new_with_free_func((GDestroyNotify)nm_ip_route_unref);
    char **atok = np_split_list(ip->addresses);
    char **dtok = np_split_list(ip->dns);
    char **stok = np_split_list(ip->dns_search);
    char **rlines = np_split_lines(ip->routes);
    gboolean ok = FALSE;

    for (char **a = atok; *a; a++) {
        NMIPAddress *addr = np_ip_address_parse(family, *a, error);
        if (!addr) goto out;
        g_ptr_array_add(addrs, addr);
    }
    if (!strcmp(ip->method, "manual") && addrs->len == 0) {
        fail(error, "%s 手动模式需要至少一个地址", fam);
        goto out;
    }
    if (!empty(ip->gateway)) {
        if (!nm_utils_ipaddr_valid(family, ip->gateway)) {
            fail(error, "%s 网关无效：%s", fam, ip->gateway);
            goto out;
        }
        if (addrs->len == 0) {
            fail(error, "设置 %s 网关需要同时填写地址", fam);
            goto out;
        }
        if (ip->never_default) {
            /* NM 规范化时会丢弃网关，这里明确报错而不是静默丢失 */
            fail(error, "启用“仅用于此网络的资源”时不能设置 %s 网关", fam);
            goto out;
        }
    }
    for (char **d = dtok; *d; d++)
        if (!nm_utils_ipaddr_valid(family, *d)) {
            fail(error, "%s DNS 地址无效：%s", fam, *d);
            goto out;
        }
    for (char **r = rlines; *r; r++) {
        NMIPRoute *route = np_ip_route_parse(family, *r, error);
        if (!route) goto out;
        g_ptr_array_add(routes, route);
    }

    g_object_set(s,
                 NM_SETTING_IP_CONFIG_METHOD, ip->method,
                 NM_SETTING_IP_CONFIG_GATEWAY, or_null(ip->gateway),
                 NM_SETTING_IP_CONFIG_NEVER_DEFAULT, ip->never_default,
                 NM_SETTING_IP_CONFIG_IGNORE_AUTO_DNS, ip->ignore_auto_dns,
                 NULL);
    nm_setting_ip_config_clear_addresses(s);
    for (guint i = 0; i < addrs->len; i++) nm_setting_ip_config_add_address(s, addrs->pdata[i]);
    nm_setting_ip_config_clear_routes(s);
    for (guint i = 0; i < routes->len; i++) nm_setting_ip_config_add_route(s, routes->pdata[i]);
    nm_setting_ip_config_clear_dns(s);
    for (char **d = dtok; *d; d++) nm_setting_ip_config_add_dns(s, *d);
    nm_setting_ip_config_clear_dns_searches(s);
    for (char **d = stok; *d; d++) nm_setting_ip_config_add_dns_search(s, *d);
    if (family == AF_INET6) g_object_set(s, NM_SETTING_IP6_CONFIG_IP6_PRIVACY, ip->ip6_privacy, NULL);
    ok = TRUE;
out:
    g_ptr_array_unref(addrs);
    g_ptr_array_unref(routes);
    g_strfreev(atok);
    g_strfreev(dtok);
    g_strfreev(stok);
    g_strfreev(rlines);
    return ok;
}

gboolean np_conn_model_apply(const NpConnModel *m, NMConnection *c, GError **error)
{
    g_return_val_if_fail(m && c, FALSE);
    if (!apply_connection(m, c, error)) return FALSE;

    const char *t = m->type;
    if (!g_strcmp0(t, NM_SETTING_WIRELESS_SETTING_NAME)) {
        if (!apply_wireless(m, c, error) || !apply_security(m, c, error)) return FALSE;
    } else if (!g_strcmp0(t, NM_SETTING_WIRED_SETTING_NAME)) {
        if (!apply_wired(m, c, error)) return FALSE;
    } else if (!g_strcmp0(t, NM_SETTING_WIREGUARD_SETTING_NAME)) {
        if (!apply_wireguard(m, c, error)) return FALSE;
    } else if (!g_strcmp0(t, NM_SETTING_VPN_SETTING_NAME)) {
        if (!apply_vpn(m, c, error)) return FALSE;
    }

    if (!apply_ip(&m->ip4, c, AF_INET, error)) return FALSE;
    if (!apply_ip(&m->ip6, c, AF_INET6, error)) return FALSE;
    if (!apply_proxy(m, c, error)) return FALSE;
    return nm_connection_normalize(c, NULL, NULL, error);
}

NMConnection *np_conn_model_create_connection(const NpConnModel *m, GError **error)
{
    NMConnection *c = nm_simple_connection_new();
    NMSetting *sc = nm_setting_connection_new();
    char *uuid = nm_utils_uuid_generate();
    g_object_set(sc, NM_SETTING_CONNECTION_UUID, uuid, NM_SETTING_CONNECTION_TYPE, m->type, NULL);
    g_free(uuid);
    nm_connection_add_setting(c, sc);
    if (!np_conn_model_apply(m, c, error)) {
        g_object_unref(c);
        return NULL;
    }
    return c;
}
