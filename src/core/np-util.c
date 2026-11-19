// SPDX-License-Identifier: GPL-3.0-or-later
#include "np-util.h"

#include <string.h>
#include <sys/socket.h>

G_DEFINE_QUARK(np-error-quark, np_error)

/* ── 频段 / 信号 ── */

NpBand np_band_from_frequency(guint32 mhz)
{
    if (mhz >= 5925) return NP_BAND_6GHZ;
    if (mhz >= 4900) return NP_BAND_5GHZ;
    return NP_BAND_2GHZ;
}

const char *np_band_label(NpBand band)
{
    switch (band) {
    case NP_BAND_6GHZ: return "6 GHz";
    case NP_BAND_5GHZ: return "5 GHz";
    default: return "2.4 GHz";
    }
}

guint np_signal_level(guint8 s)
{
    return s >= 75 ? 4 : s >= 50 ? 3 : s >= 25 ? 2 : s > 0 ? 1 : 0;
}

/* ── SSID ── */

char *np_ssid_to_display(GBytes *ssid)
{
    if (!ssid || !g_bytes_get_size(ssid)) return g_strdup("");
    gsize len;
    const guint8 *d = g_bytes_get_data(ssid, &len);
    return nm_utils_ssid_to_utf8(d, len);
}

GBytes *np_ssid_from_text(const char *text, GError **error)
{
    gsize len = text ? strlen(text) : 0;
    if (len == 0) {
        g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, "网络名称 (SSID) 不能为空");
        return NULL;
    }
    if (len > 32) {
        g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, "网络名称 (SSID) 不能超过 32 字节");
        return NULL;
    }
    return g_bytes_new(text, len);
}

/* ── AP 安全类型 ── */

NpApSecurity np_ap_security_classify(guint32 flags, guint32 wpa, guint32 rsn)
{
    guint32 all = wpa | rsn;
    if (all & (NM_802_11_AP_SEC_KEY_MGMT_802_1X | NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192))
        return NP_AP_SEC_ENTERPRISE;
    gboolean psk = !!(all & NM_802_11_AP_SEC_KEY_MGMT_PSK);
    gboolean sae = !!(rsn & NM_802_11_AP_SEC_KEY_MGMT_SAE);
    if (psk && sae) return NP_AP_SEC_WPA2_WPA3;
    if (sae) return NP_AP_SEC_WPA3;
    if (psk) return (rsn & NM_802_11_AP_SEC_KEY_MGMT_PSK) ? NP_AP_SEC_WPA2 : NP_AP_SEC_WPA;
    if (rsn & (NM_802_11_AP_SEC_KEY_MGMT_OWE | NM_802_11_AP_SEC_KEY_MGMT_OWE_TM)) return NP_AP_SEC_OWE;
    if ((flags & NM_802_11_AP_FLAGS_PRIVACY) && !wpa && !rsn) return NP_AP_SEC_WEP;
    return NP_AP_SEC_OPEN;
}

const char *np_ap_security_label(NpApSecurity sec)
{
    switch (sec) {
    case NP_AP_SEC_OWE: return "增强开放";
    case NP_AP_SEC_WEP: return "WEP";
    case NP_AP_SEC_WPA: return "WPA";
    case NP_AP_SEC_WPA2: return "WPA2";
    case NP_AP_SEC_WPA2_WPA3: return "WPA2/3";
    case NP_AP_SEC_WPA3: return "WPA3";
    case NP_AP_SEC_ENTERPRISE: return "企业级";
    default: return "开放";
    }
}

gboolean np_ap_security_is_secure(NpApSecurity sec)
{
    return sec != NP_AP_SEC_OPEN && sec != NP_AP_SEC_OWE;
}

NpQuickConnect np_ap_security_quick_connect(NpApSecurity sec)
{
    switch (sec) {
    case NP_AP_SEC_OPEN:
    case NP_AP_SEC_OWE: return NP_QUICK_OPEN;
    case NP_AP_SEC_WEP:
    case NP_AP_SEC_ENTERPRISE: return NP_QUICK_EDITOR;
    default: return NP_QUICK_PSK;
    }
}

const char *np_ap_security_key_mgmt(NpApSecurity sec)
{
    switch (sec) {
    case NP_AP_SEC_WPA:
    case NP_AP_SEC_WPA2:
    case NP_AP_SEC_WPA2_WPA3: return "wpa-psk";
    case NP_AP_SEC_WPA3: return "sae";
    default: return NULL;
    }
}

/* ── 密钥校验 ── */

static gboolean all_hex(const char *s)
{
    for (; *s; s++)
        if (!g_ascii_isxdigit(*s)) return FALSE;
    return TRUE;
}

gboolean np_psk_validate(const char *key_mgmt, const char *psk, GError **error)
{
    gsize n = psk ? strlen(psk) : 0;
    if (n == 0) {
        g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, "请输入密码");
        return FALSE;
    }
    if (g_strcmp0(key_mgmt, "sae") == 0) return TRUE;
    if (n == 64) {
        if (all_hex(psk)) return TRUE;
        g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, "64 位密钥必须是十六进制");
        return FALSE;
    }
    if (n < 8 || n > 63) {
        g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, "WPA 密码长度应为 8–63 个字符");
        return FALSE;
    }
    return TRUE;
}

NMWepKeyType np_wep_key_type_guess(const char *key)
{
    gsize n = key ? strlen(key) : 0;
    if ((n == 10 || n == 26) && all_hex(key)) return NM_WEP_KEY_TYPE_KEY;
    if (n == 5 || n == 13) return NM_WEP_KEY_TYPE_KEY;
    return NM_WEP_KEY_TYPE_PASSPHRASE;
}

/* ── 列表 ── */

static char **split_by(const char *text, const char *seps)
{
    GPtrArray *a = g_ptr_array_new();
    const char *p = text ? text : "";
    while (*p) {
        while (*p && strchr(seps, *p)) p++;
        const char *s = p;
        while (*p && !strchr(seps, *p)) p++;
        if (p > s) g_ptr_array_add(a, g_strndup(s, p - s));
    }
    g_ptr_array_add(a, NULL);
    return (char **)g_ptr_array_free(a, FALSE);
}

char **np_split_list(const char *text)
{
    return split_by(text, " \t\r\n,;");
}

char **np_split_lines(const char *text)
{
    char **raw = split_by(text, "\r\n");
    GPtrArray *a = g_ptr_array_new();
    for (char **l = raw; *l; l++) {
        char *t = g_strstrip(*l);
        if (*t && *t != '#') g_ptr_array_add(a, g_strdup(t));
    }
    g_strfreev(raw);
    g_ptr_array_add(a, NULL);
    return (char **)g_ptr_array_free(a, FALSE);
}

/* ── 地址 / 路由 ── */

static const char *family_name(int family)
{
    return family == AF_INET6 ? "IPv6" : "IPv4";
}

gboolean np_parse_prefixed(int family, const char *text, guint default_prefix, guint min_prefix,
                           char **addr_out, guint *prefix_out, GError **error)
{
    guint max = family == AF_INET6 ? 128 : 32;
    char **parts = g_strsplit(text ? text : "", "/", 2);
    gboolean ok = FALSE;

    if (!parts[0] || !nm_utils_ipaddr_valid(family, parts[0])) {
        g_set_error(error, NP_ERROR, NP_ERROR_INVALID, "无效的 %s 地址：%s", family_name(family),
                    text ? text : "");
        goto out;
    }
    guint prefix = default_prefix;
    if (parts[1]) {
        char *end = NULL;
        guint64 v = g_ascii_strtoull(parts[1], &end, 10);
        if (!*parts[1] || *end || v < min_prefix || v > max) {
            g_set_error(error, NP_ERROR, NP_ERROR_INVALID, "无效的前缀长度：%s", text);
            goto out;
        }
        prefix = (guint)v;
    }
    if (addr_out) *addr_out = g_strdup(parts[0]);
    if (prefix_out) *prefix_out = prefix;
    ok = TRUE;
out:
    g_strfreev(parts);
    return ok;
}

NMIPAddress *np_ip_address_parse(int family, const char *text, GError **error)
{
    char *addr = NULL;
    guint prefix = 0;
    if (!np_parse_prefixed(family, text, family == AF_INET6 ? 64 : 24, 1, &addr, &prefix, error))
        return NULL;
    NMIPAddress *a = nm_ip_address_new(family, addr, prefix, error);
    g_free(addr);
    return a;
}

char *np_ip_address_format(NMIPAddress *a)
{
    return g_strdup_printf("%s/%u", nm_ip_address_get_address(a), nm_ip_address_get_prefix(a));
}

static gboolean parse_uint(const char *s, guint64 max, guint64 *out)
{
    char *end = NULL;
    if (!s || !*s || !g_ascii_isdigit(*s)) return FALSE;
    guint64 v = g_ascii_strtoull(s, &end, 10);
    if (*end || v > max) return FALSE;
    *out = v;
    return TRUE;
}

NMIPRoute *np_ip_route_parse(int family, const char *line, GError **error)
{
    char **tok = np_split_list(line);
    char *dest = NULL;
    const char *gw = NULL;
    gint64 metric = -1;
    guint prefix = 0;
    NMIPRoute *route = NULL;

    if (!tok[0]) {
        g_set_error_literal(error, NP_ERROR, NP_ERROR_INVALID, "路由不能为空");
        goto out;
    }
    if (!np_parse_prefixed(family, tok[0], family == AF_INET6 ? 128 : 32, 0, &dest, &prefix, error))
        goto out;

    for (guint i = 1; tok[i]; i++) {
        guint64 v;
        if (!strcmp(tok[i], "via") && tok[i + 1]) {
            gw = tok[++i];
            if (!nm_utils_ipaddr_valid(family, gw)) goto bad;
        } else if (!strcmp(tok[i], "metric") && tok[i + 1]) {
            if (!parse_uint(tok[++i], G_MAXUINT32, &v)) goto bad;
            metric = (gint64)v;
        } else if (!gw && nm_utils_ipaddr_valid(family, tok[i])) {
            gw = tok[i];
        } else if (metric < 0 && parse_uint(tok[i], G_MAXUINT32, &v)) {
            metric = (gint64)v;
        } else {
            goto bad;
        }
    }
    route = nm_ip_route_new(family, dest, prefix, gw, metric, error);
    goto out;
bad:
    g_set_error(error, NP_ERROR, NP_ERROR_INVALID, "无法解析路由：%s（格式：目标/前缀 [via 网关] [metric 跃点]）", line);
out:
    g_free(dest);
    g_strfreev(tok);
    return route;
}

char *np_ip_route_format(NMIPRoute *r)
{
    GString *s = g_string_new(NULL);
    g_string_append_printf(s, "%s/%u", nm_ip_route_get_dest(r), nm_ip_route_get_prefix(r));
    if (nm_ip_route_get_next_hop(r)) g_string_append_printf(s, " via %s", nm_ip_route_get_next_hop(r));
    if (nm_ip_route_get_metric(r) >= 0)
        g_string_append_printf(s, " metric %" G_GINT64_FORMAT, nm_ip_route_get_metric(r));
    return g_string_free(s, FALSE);
}

/* ── 键值 ── */

GHashTable *np_kv_parse(const char *text, GError **error)
{
    GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    char **lines = g_strsplit(text ? text : "", "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        char *l = g_strstrip(lines[i]);
        if (!*l || *l == '#') continue;
        char *eq = strchr(l, '=');
        if (!eq) {
            g_set_error(error, NP_ERROR, NP_ERROR_INVALID, "第 %u 行缺少 “=”：%s", i + 1, l);
            goto fail;
        }
        *eq = '\0';
        char *k = g_strstrip(l), *v = g_strstrip(eq + 1);
        if (!*k) {
            g_set_error(error, NP_ERROR, NP_ERROR_INVALID, "第 %u 行的键为空", i + 1);
            goto fail;
        }
        g_hash_table_replace(t, g_strdup(k), g_strdup(v));
    }
    g_strfreev(lines);
    return t;
fail:
    g_strfreev(lines);
    g_hash_table_unref(t);
    return NULL;
}

static gint cmp_str(gconstpointer a, gconstpointer b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

char *np_kv_format(GHashTable *table)
{
    GString *s = g_string_new(NULL);
    if (!table) return g_string_free(s, FALSE);
    guint n;
    const char **keys = (const char **)g_hash_table_get_keys_as_array(table, &n);
    qsort(keys, n, sizeof *keys, (int (*)(const void *, const void *))cmp_str);
    for (guint i = 0; i < n; i++)
        g_string_append_printf(s, "%s = %s\n", keys[i], (const char *)g_hash_table_lookup(table, keys[i]));
    g_free(keys);
    return g_string_free(s, FALSE);
}

char *np_expand_home(const char *path)
{
    if (!path) return NULL;
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0'))
        return g_build_filename(g_get_home_dir(), path[1] ? path + 2 : "", NULL);
    return g_strdup(path);
}
