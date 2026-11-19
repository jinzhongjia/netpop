#include "np-actions.h"
#include "np-app.h"

NMActiveConnection *np_active_for(NMRemoteConnection *rc)
{
    if (!np->client || !rc) return NULL;
    const GPtrArray *acs = nm_client_get_active_connections(np->client);
    for (guint i = 0; i < acs->len; i++) {
        NMActiveConnection *ac = acs->pdata[i];
        if (nm_active_connection_get_connection(ac) == rc) return ac;
    }
    return NULL;
}

NMDevice *np_ac_device(NMActiveConnection *ac)
{
    const GPtrArray *d = ac ? nm_active_connection_get_devices(ac) : NULL;
    return d && d->len ? d->pdata[0] : NULL;
}

char *np_ac_ipv4(NMActiveConnection *ac)
{
    NMIPConfig *cfg = ac ? nm_active_connection_get_ip4_config(ac) : NULL;
    GPtrArray *a = cfg ? nm_ip_config_get_addresses(cfg) : NULL;
    if (!a || !a->len) return NULL;
    NMIPAddress *ip = a->pdata[0];
    return g_strdup_printf("%s/%u", nm_ip_address_get_address(ip), nm_ip_address_get_prefix(ip));
}

GPtrArray *np_wifi_devices(void)
{
    GPtrArray *out = g_ptr_array_new();
    if (!np->client) return out;
    const GPtrArray *devs = nm_client_get_devices(np->client);
    for (guint i = 0; i < devs->len; i++) {
        NMDevice *d = devs->pdata[i];
        if (NM_IS_DEVICE_WIFI(d) && nm_device_get_state(d) > NM_DEVICE_STATE_UNMANAGED) g_ptr_array_add(out, d);
    }
    return out;
}

static gboolean is_hotspot(NMConnection *c)
{
    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    return sw && !g_strcmp0(nm_setting_wireless_get_mode(sw), NM_SETTING_WIRELESS_MODE_AP);
}

const char *np_conn_glyph(NMConnection *c)
{
    const char *t = nm_connection_get_connection_type(c);
    if (!g_strcmp0(t, NM_SETTING_WIRELESS_SETTING_NAME)) return is_hotspot(c) ? NP_G_HOTSPOT : NP_G_WIFI;
    if (!g_strcmp0(t, NM_SETTING_WIRED_SETTING_NAME)) return NP_G_ETHERNET;
    if (!g_strcmp0(t, NM_SETTING_VPN_SETTING_NAME) || !g_strcmp0(t, NM_SETTING_WIREGUARD_SETTING_NAME)) return NP_G_VPN;
    return NP_G_NETWORK;
}

const char *np_conn_type_label(NMConnection *c)
{
    static const struct { const char *type, *label; } map[] = {
        { NM_SETTING_WIRELESS_SETTING_NAME, "Wi-Fi" },  { NM_SETTING_WIRED_SETTING_NAME, "有线" },
        { NM_SETTING_VPN_SETTING_NAME, "VPN" },          { NM_SETTING_WIREGUARD_SETTING_NAME, "WireGuard" },
        { NM_SETTING_BRIDGE_SETTING_NAME, "网桥" },      { NM_SETTING_BOND_SETTING_NAME, "链路聚合" },
        { NM_SETTING_VLAN_SETTING_NAME, "VLAN" },        { NM_SETTING_GSM_SETTING_NAME, "移动宽带" },
        { NM_SETTING_BLUETOOTH_SETTING_NAME, "蓝牙" },   { NM_SETTING_LOOPBACK_SETTING_NAME, "回环" },
        { NM_SETTING_TUN_SETTING_NAME, "TUN/TAP" },
    };
    const char *t = nm_connection_get_connection_type(c);
    if (!g_strcmp0(t, NM_SETTING_WIRELESS_SETTING_NAME) && is_hotspot(c)) return "热点";
    for (guint i = 0; i < G_N_ELEMENTS(map); i++)
        if (!g_strcmp0(map[i].type, t)) return map[i].label;
    return t ? t : "未知";
}

const char *np_ac_state_label(NMActiveConnection *ac)
{
    switch (ac ? nm_active_connection_get_state(ac) : NM_ACTIVE_CONNECTION_STATE_UNKNOWN) {
    case NM_ACTIVE_CONNECTION_STATE_ACTIVATED: return "已连接";
    case NM_ACTIVE_CONNECTION_STATE_ACTIVATING: return "正在连接…";
    case NM_ACTIVE_CONNECTION_STATE_DEACTIVATING: return "正在断开…";
    default: return "未连接";
    }
}

/* ── 激活跟踪：把结果告诉用户，并清理从未成功的新连接 ── */

typedef struct {
    NMActiveConnection *ac;
    char *name;
    char *conn_path;
    gboolean is_new;
} Attempt;

static const char *fail_reason(NMActiveConnection *ac, guint reason)
{
    if (reason == NM_ACTIVE_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED) {
        NMDevice *d = np_ac_device(ac);
        switch (d ? nm_device_get_state_reason(d) : 0) {
        case NM_DEVICE_STATE_REASON_NO_SECRETS:
        case NM_DEVICE_STATE_REASON_SUPPLICANT_DISCONNECT: return "密码错误";
        case NM_DEVICE_STATE_REASON_SUPPLICANT_TIMEOUT: return "认证超时";
        case NM_DEVICE_STATE_REASON_SSID_NOT_FOUND: return "找不到该网络";
        case NM_DEVICE_STATE_REASON_IP_CONFIG_UNAVAILABLE: return "获取 IP 失败";
        default: return "设备已断开";
        }
    }
    switch (reason) {
    case NM_ACTIVE_CONNECTION_STATE_REASON_NO_SECRETS: return "未提供密码";
    case NM_ACTIVE_CONNECTION_STATE_REASON_LOGIN_FAILED: return "认证失败";
    case NM_ACTIVE_CONNECTION_STATE_REASON_CONNECT_TIMEOUT: return "连接超时";
    case NM_ACTIVE_CONNECTION_STATE_REASON_IP_CONFIG_INVALID: return "IP 配置无效";
    case NM_ACTIVE_CONNECTION_STATE_REASON_SERVICE_START_FAILED: return "VPN 服务启动失败";
    case NM_ACTIVE_CONNECTION_STATE_REASON_DEPENDENCY_FAILED: return "依赖的连接失败";
    default: return "连接失败";
    }
}

static void attempt_free(Attempt *t)
{
    g_signal_handlers_disconnect_by_data(t->ac, t);
    g_object_unref(t->ac);
    g_free(t->name);
    g_free(t->conn_path);
    g_free(t);
}

static void attempt_state(NMActiveConnection *ac, guint state, guint reason, Attempt *t)
{
    if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
        np_notify("已连接", "%s", t->name);
        attempt_free(t);
    } else if (state == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED) {
        if (reason != NM_ACTIVE_CONNECTION_STATE_REASON_USER_DISCONNECTED)
            np_notify("连接失败", "%s：%s", t->name, fail_reason(ac, reason));
        if (t->is_new && t->conn_path) {
            NMRemoteConnection *rc = nm_client_get_connection_by_path(np->client, t->conn_path);
            if (rc) nm_remote_connection_delete_async(rc, NULL, NULL, NULL);
        }
        attempt_free(t);
    }
}

static void watch(NMActiveConnection *ac, const char *name, gboolean is_new)
{
    Attempt *t = g_new0(Attempt, 1);
    t->ac = g_object_ref(ac);
    t->name = g_strdup(name);
    t->is_new = is_new;
    NMRemoteConnection *rc = nm_active_connection_get_connection(ac);
    if (rc) t->conn_path = g_strdup(nm_object_get_path(NM_OBJECT(rc)));
    g_signal_connect(ac, "state-changed", G_CALLBACK(attempt_state), t);
    NMActiveConnectionState s = nm_active_connection_get_state(ac);
    if (s == NM_ACTIVE_CONNECTION_STATE_ACTIVATED || s == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
        attempt_state(ac, s, nm_active_connection_get_state_reason(ac), t);
}

static void activate_done(GObject *src, GAsyncResult *res, gpointer data)
{
    char *name = data;
    GError *e = NULL;
    NMActiveConnection *ac = nm_client_activate_connection_finish(NM_CLIENT(src), res, &e);
    if (e) {
        np_notify("连接失败", "%s：%s", name, e->message);
        g_error_free(e);
    } else {
        watch(ac, name, FALSE);
        g_object_unref(ac);
    }
    g_free(name);
    np_queue_refresh();
}

static void add_activate_done(GObject *src, GAsyncResult *res, gpointer data)
{
    char *name = data;
    GError *e = NULL;
    NMActiveConnection *ac = nm_client_add_and_activate_connection_finish(NM_CLIENT(src), res, &e);
    if (e) {
        np_notify("连接失败", "%s：%s", name, e->message);
        g_error_free(e);
    } else {
        watch(ac, name, TRUE);
        g_object_unref(ac);
    }
    g_free(name);
    np_queue_refresh();
}

void np_activate(NMRemoteConnection *rc, NMDevice *dev, const char *specific)
{
    const char *name = nm_connection_get_id(NM_CONNECTION(rc));
    nm_client_activate_connection_async(np->client, NM_CONNECTION(rc), dev, specific, NULL, activate_done,
                                        g_strdup(name));
    np_notify(NULL, "正在连接 %s…", name);
}

static void deactivate_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    if (!nm_client_deactivate_connection_finish(NM_CLIENT(src), res, &e)) {
        np_notify("断开失败", "%s", e->message);
        g_error_free(e);
    }
}

void np_deactivate(NMRemoteConnection *rc)
{
    NMActiveConnection *ac = np_active_for(rc);
    if (ac) nm_client_deactivate_connection_async(np->client, ac, NULL, deactivate_done, NULL);
}

void np_connect_new(NMConnection *partial, NMDevice *dev, const char *ap_path, const char *name)
{
    nm_client_add_and_activate_connection_async(np->client, partial, dev, ap_path, NULL, add_activate_done,
                                                g_strdup(name));
    np_notify(NULL, "正在连接 %s…", name);
}

static void delete_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    if (!nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(src), res, &e)) {
        np_notify("删除失败", "%s", e->message);
        g_error_free(e);
    }
}

void np_forget(NMRemoteConnection *rc)
{
    np_notify(NULL, "已删除 %s", nm_connection_get_id(NM_CONNECTION(rc)));
    nm_remote_connection_delete_async(rc, NULL, delete_done, NULL);
}

/* ── 热应用 ── */

typedef struct {
    NMRemoteConnection *rc;
    NMDevice *dev;
} Reapply;

static void reapply_done(GObject *src, GAsyncResult *res, gpointer data)
{
    Reapply *r = data;
    GError *e = NULL;
    if (!nm_device_reapply_finish(NM_DEVICE(src), res, &e)) {
        g_debug("reapply failed, reactivating: %s", e->message);
        g_error_free(e);
        np_activate(r->rc, r->dev, NULL);
    }
    g_object_unref(r->rc);
    g_object_unref(r->dev);
    g_free(r);
}

void np_apply_to_active(NMRemoteConnection *rc, NMConnection *applied, gboolean force)
{
    NMActiveConnection *ac = np_active_for(rc);
    if (!ac) return;
    NMDevice *dev = np_ac_device(ac);
    if (force || !dev) {
        np_activate(rc, dev, NULL);
        return;
    }
    Reapply *r = g_new0(Reapply, 1);
    r->rc = g_object_ref(rc);
    r->dev = g_object_ref(dev);
    nm_device_reapply_async(dev, applied, 0, 0, NULL, reapply_done, r);
}

/* ── 扫描 / 开关 ── */

void np_request_scan(gint64 min_age_ms)
{
    GPtrArray *devs = np_wifi_devices();
    gint64 now = nm_utils_get_timestamp_msec();
    for (guint i = 0; i < devs->len; i++) {
        gint64 last = nm_device_wifi_get_last_scan(devs->pdata[i]);
        if (last < 0 || now - last >= min_age_ms)
            nm_device_wifi_request_scan_async(devs->pdata[i], NULL, NULL, NULL);
    }
    g_ptr_array_unref(devs);
}

static void set_prop_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    if (!nm_client_dbus_set_property_finish(NM_CLIENT(src), res, &e)) {
        np_notify("操作失败", "%s", e->message);
        g_error_free(e);
        np_queue_refresh();
    }
}

static void set_bool(const char *prop, gboolean v)
{
    if (!np->client) return;
    nm_client_dbus_set_property(np->client, NM_DBUS_PATH, NM_DBUS_INTERFACE, prop, g_variant_new_boolean(v), -1,
                                NULL, set_prop_done, NULL);
}

void np_set_wireless_enabled(gboolean v) { set_bool("WirelessEnabled", v); }

static void networking_done(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &e);
    if (r) g_variant_unref(r);
    if (e) {
        np_notify("操作失败", "%s", e->message);
        g_error_free(e);
        np_queue_refresh();
    }
}

void np_set_networking_enabled(gboolean v)
{
    /* NetworkingEnabled 是只读属性，需要调用 Enable() 方法 */
    if (!np->client) return;
    g_dbus_connection_call(nm_client_get_dbus_connection(np->client), NM_DBUS_SERVICE, NM_DBUS_PATH,
                           NM_DBUS_INTERFACE, "Enable", g_variant_new("(b)", v), NULL,
                           G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION, -1, NULL, networking_done, NULL);
}
