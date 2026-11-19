// SPDX-License-Identifier: GPL-3.0-or-later
/* np-conn-model.h — 连接编辑器的数据模型
 *
 * 编辑器界面只和 NpConnModel 打交道：
 *   NMConnection --np_conn_model_from_connection--> NpConnModel --(界面编辑)-->
 *   np_conn_model_apply() --> NMConnection
 * 所有校验与 NM 设置的构造都在这里完成，因此可以脱离界面和 NetworkManager 守护进程做单元测试。
 *
 * 约定：密钥类字段（psk、密码、私钥等）为空表示“保持不变”。
 */
#pragma once

#include "np-util.h"

G_BEGIN_DECLS

typedef struct {
    const char *value;
    const char *label;
} NpChoice;

typedef enum {
    NP_SEC_NONE,
    NP_SEC_WPA_PSK,
    NP_SEC_SAE,
    NP_SEC_EAP,
    NP_SEC_WEP,
    NP_SEC_OWE,
    NP_SEC_N,
} NpSecType;

typedef enum {
    NP_EAP_PEAP,
    NP_EAP_TTLS,
    NP_EAP_TLS,
    NP_EAP_PWD,
    NP_EAP_N,
} NpEapMethod;

#define NP_PHASE2_N    4
#define NP_IP4_METHOD_N 5
#define NP_IP6_METHOD_N 6
#define NP_MAC_N       5
#define NP_BAND_N      3
#define NP_METERED_N   3
#define NP_PRIVACY_N   4

extern const NpChoice np_sec_choices[NP_SEC_N];
extern const NpChoice np_eap_choices[NP_EAP_N];
extern const NpChoice np_phase2_choices[NP_PHASE2_N];
extern const NpChoice np_ip4_method_choices[NP_IP4_METHOD_N];
extern const NpChoice np_ip6_method_choices[NP_IP6_METHOD_N];
extern const NpChoice np_mac_choices[NP_MAC_N];    /* cloned-mac-address 预设，value 为 NULL 表示默认 */
extern const NpChoice np_band_choices[NP_BAND_N];  /* value 为 NULL 表示自动 */
extern const NpChoice np_metered_choices[NP_METERED_N]; /* 下标即 NMMetered 值 */
extern const NpChoice np_privacy_choices[NP_PRIVACY_N]; /* 下标 = ip6-privacy + 1 */

/* 在 choices 中查找 value（NULL 与 NULL 匹配），找不到返回 n */
guint np_choice_index(const NpChoice *choices, guint n, const char *value);

typedef struct {
    char    *method;
    char    *addresses;  /* 每行或逗号分隔 “地址/前缀” */
    char    *gateway;
    char    *dns;        /* 逗号 / 空白分隔 */
    char    *dns_search;
    char    *routes;     /* 每行一条 “目标/前缀 [via 网关] [metric N]” */
    gboolean never_default;
    gboolean ignore_auto_dns;
    int      ip6_privacy; /* 仅 IPv6：-1 默认，0 禁用，1/2 启用 */
} NpIpModel;

typedef struct {
    char *public_key;
    char *endpoint;
    char *allowed_ips;
    char *preshared_key; /* 空 = 保持不变 */
    guint keepalive;
} NpWgPeerModel;

typedef struct {
    char     *type; /* NM 设置名：802-11-wireless / 802-3-ethernet / wireguard / vpn / … */
    char     *uuid;

    /* 通用 */
    char     *id;
    gboolean  autoconnect;
    int       autoconnect_priority;
    NMMetered metered;
    gboolean  all_users;
    char     *interface_name;

    /* Wi-Fi / 有线 */
    char     *ssid;
    gboolean  ap_mode;
    char     *band;
    gboolean  hidden;
    char     *cloned_mac;
    guint     mtu;

    /* Wi-Fi 安全 */
    NpSecType   security;
    char       *psk;
    char       *wep_key;
    NpEapMethod eap;
    char       *identity;
    char       *anonymous_identity;
    char       *eap_password;
    char       *phase2;
    char       *ca_cert;
    gboolean    system_ca_certs;
    char       *domain_suffix_match;
    char       *client_cert;
    char       *private_key;
    char       *private_key_password;

    /* WireGuard */
    char      *wg_private_key;
    guint      wg_listen_port;
    GPtrArray *wg_peers; /* NpWgPeerModel* */

    /* VPN（插件无关的通用编辑） */
    char *vpn_service;
    char *vpn_user;
    char *vpn_data;    /* key = value 行 */
    char *vpn_secrets; /* key = value 行；空 = 保持不变 */

    /* 代理 */
    gboolean proxy_auto;
    char    *pac_url;

    NpIpModel ip4, ip6;
} NpConnModel;

NpWgPeerModel *np_wg_peer_model_new(void);
void           np_wg_peer_model_free(NpWgPeerModel *p);

NpConnModel *np_conn_model_new(const char *type);
NpConnModel *np_conn_model_new_hotspot(const char *ssid, const char *psk);
NpConnModel *np_conn_model_from_connection(NMConnection *connection);
void         np_conn_model_free(NpConnModel *m);

/* g_free 旧值并复制新值 */
void np_set_str(char **field, const char *value);

/* 把模型写入 connection（原地修改），最后调用 nm_connection_normalize() 校验 */
gboolean      np_conn_model_apply(const NpConnModel *m, NMConnection *connection, GError **error);
/* 新建连接（生成 UUID）并写入模型 */
NMConnection *np_conn_model_create_connection(const NpConnModel *m, GError **error);

NpSecType np_conn_security_type(NMConnection *connection);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NpConnModel, np_conn_model_free)

G_END_DECLS
