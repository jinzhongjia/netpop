// SPDX-License-Identifier: GPL-3.0-or-later
/* np-ap.h — 把扫描到的 AP 合并为“网络”（同 SSID 合一）并排序 */
#pragma once

#include "np-util.h"

G_BEGIN_DECLS

/* 输入：一个 AP 的快照。ap / device 为可选的 GObject，会被引用。 */
typedef struct {
    GBytes      *ssid;
    guint8       strength;
    guint32      frequency;
    guint32      flags, wpa_flags, rsn_flags;
    NM80211Mode  mode;
    gboolean     is_active; /* 是设备当前连接的 AP */
    GObject     *ap;
    GObject     *device;
} NpApInput;

typedef struct {
    GBytes      *ssid;
    char        *name;
    guint8       strength;
    guint32      frequency;
    NpApSecurity security;
    GObject     *ap;
    GObject     *device;
    GObject     *connection; /* 已保存 / 正在使用的连接（由调用方设置） */
    gboolean     active;
    gboolean     activating;
} NpNetwork;

void       np_network_free(NpNetwork *net);
void       np_network_set_connection(NpNetwork *net, gpointer connection);

/* 返回 NpNetwork 数组（自带 free 函数）。隐藏 SSID 和非基础设施模式的 AP 会被跳过；
 * 同名网络保留当前连接的 AP，否则保留信号最强的。 */
GPtrArray *np_networks_from_aps(const NpApInput *aps, gsize n_aps);
NpNetwork *np_networks_find(GPtrArray *nets, GBytes *ssid);

/* 排序：已连接 → 正在连接 → 已保存 → 其他；同级按信号强度降序，再按名称 */
gint       np_network_compare(const NpNetwork *a, const NpNetwork *b);
void       np_networks_sort(GPtrArray *nets);

G_END_DECLS
