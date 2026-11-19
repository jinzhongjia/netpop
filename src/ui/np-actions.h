/* np-actions.h — 对 NetworkManager 的操作（异步）以及结果通知 */
#pragma once

#include <NetworkManager.h>

G_BEGIN_DECLS

NMActiveConnection *np_active_for(NMRemoteConnection *rc);
NMDevice           *np_ac_device(NMActiveConnection *ac);
char               *np_ac_ipv4(NMActiveConnection *ac);
GPtrArray          *np_wifi_devices(void); /* 元素为借用的 NMDeviceWifi* */

const char *np_conn_glyph(NMConnection *c);
const char *np_conn_type_label(NMConnection *c);
const char *np_ac_state_label(NMActiveConnection *ac);

void np_activate(NMRemoteConnection *rc, NMDevice *device, const char *specific_object);
void np_deactivate(NMRemoteConnection *rc);
/* partial 可以为 NULL（让 NM 根据 AP 补全）；新建的连接若从未连上会被自动删除 */
void np_connect_new(NMConnection *partial, NMDevice *device, const char *ap_path, const char *name);
void np_forget(NMRemoteConnection *rc);

/* 保存后把改动应用到正在使用的连接：优先热应用，失败则重新激活 */
void np_apply_to_active(NMRemoteConnection *rc, NMConnection *applied, gboolean force_reactivate);

void np_request_scan(gint64 min_age_ms);
void np_set_wireless_enabled(gboolean enabled);
void np_set_networking_enabled(gboolean enabled);

G_END_DECLS
