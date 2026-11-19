// SPDX-License-Identifier: GPL-3.0-or-later
/* np-wg.h — WireGuard 密钥工具（X25519）与配置导入 */
#pragma once

#include <NetworkManager.h>
#include <stdint.h>

G_BEGIN_DECLS

/* RFC 7748 X25519 */
void np_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void np_x25519_base(uint8_t out[32], const uint8_t scalar[32]);

gboolean np_wg_key_valid(const char *base64_key);
char    *np_wg_generate_private_key(void);             /* Base64，已按 Curve25519 规则钳位 */
char    *np_wg_public_key(const char *private_base64); /* 无效输入返回 NULL */

/* 导入 wg-quick 格式的 .conf，返回未保存的新连接 */
NMConnection *np_wg_import(const char *path, GError **error);

G_END_DECLS
