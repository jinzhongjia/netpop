/* np-util.h — 与界面无关的基础工具：SSID、频段、安全类型、地址/路由/列表解析 */
#pragma once

#include <NetworkManager.h>

G_BEGIN_DECLS

#define NP_ERROR (np_error_quark())
GQuark np_error_quark(void);

typedef enum {
    NP_ERROR_INVALID,
    NP_ERROR_UNSUPPORTED,
    NP_ERROR_FAILED,
} NpErrorCode;

/* ── 频段 / 信号 ── */

typedef enum {
    NP_BAND_2GHZ,
    NP_BAND_5GHZ,
    NP_BAND_6GHZ,
} NpBand;

NpBand      np_band_from_frequency(guint32 mhz);
const char *np_band_label(NpBand band);
guint       np_signal_level(guint8 strength); /* 0..4 */

/* ── SSID ── */

char   *np_ssid_to_display(GBytes *ssid);
GBytes *np_ssid_from_text(const char *text, GError **error);

/* ── AP 安全类型 ── */

typedef enum {
    NP_AP_SEC_OPEN,
    NP_AP_SEC_OWE,
    NP_AP_SEC_WEP,
    NP_AP_SEC_WPA,
    NP_AP_SEC_WPA2,
    NP_AP_SEC_WPA2_WPA3,
    NP_AP_SEC_WPA3,
    NP_AP_SEC_ENTERPRISE,
} NpApSecurity;

/* 点击一个未保存的网络时该怎么连接 */
typedef enum {
    NP_QUICK_OPEN,   /* 无需凭据，直接连接 */
    NP_QUICK_PSK,    /* 弹出密码页 */
    NP_QUICK_EDITOR, /* 需要完整编辑器（企业级 / WEP） */
} NpQuickConnect;

NpApSecurity   np_ap_security_classify(guint32 flags, guint32 wpa_flags, guint32 rsn_flags);
const char    *np_ap_security_label(NpApSecurity sec);
gboolean       np_ap_security_is_secure(NpApSecurity sec);
NpQuickConnect np_ap_security_quick_connect(NpApSecurity sec);
const char    *np_ap_security_key_mgmt(NpApSecurity sec); /* 快速连接使用的 key-mgmt，或 NULL */

/* ── 密钥校验 ── */

gboolean     np_psk_validate(const char *key_mgmt, const char *psk, GError **error);
NMWepKeyType np_wep_key_type_guess(const char *key);

/* ── 列表 / 地址 / 路由 / 键值 ── */

char **np_split_list(const char *text);  /* 以空白、逗号、分号分隔，丢弃空项 */
char **np_split_lines(const char *text); /* 按行分隔，去首尾空白，丢弃空行和 # 注释 */

gboolean np_parse_prefixed(int family, const char *text, guint default_prefix, guint min_prefix,
                           char **addr_out, guint *prefix_out, GError **error);
NMIPAddress *np_ip_address_parse(int family, const char *text, GError **error);
char        *np_ip_address_format(NMIPAddress *addr);
NMIPRoute   *np_ip_route_parse(int family, const char *line, GError **error);
char        *np_ip_route_format(NMIPRoute *route);

GHashTable *np_kv_parse(const char *text, GError **error); /* "key = value" 每行一项 */
char       *np_kv_format(GHashTable *table);               /* 按 key 排序输出 */

char *np_expand_home(const char *path);

G_END_DECLS
