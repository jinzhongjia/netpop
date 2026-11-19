// SPDX-License-Identifier: GPL-3.0-or-later
/* 主页：Wi-Fi 开关、网络列表、其他连接的快捷切换 */
#include "np-actions.h"
#include "np-ap.h"
#include "np-app.h"
#include "np-pages.h"
#include "np-widgets.h"

static struct {
    GtkSwitch *wifi_switch;
    GtkWidget *scan_btn, *scan_glyph, *scan_spin;
    GtkScrolledWindow *scroller;
    GtkListBox *wifi_list, *other_list;
    GtkLabel *placeholder;
    GtkWidget *other_section;
    gboolean updating;
    guint scan_anim;
    double restore_scroll;
} W;

static const char *strength_glyph(guint8 s)
{
    static const char *g[] = { NP_G_W0, NP_G_W1, NP_G_W2, NP_G_W3, NP_G_W4 };
    return g[np_signal_level(s)];
}

static GBytes *conn_ssid(NMConnection *c)
{
    NMSettingWireless *sw = nm_connection_get_setting_wireless(c);
    if (!sw || !g_strcmp0(nm_setting_wireless_get_mode(sw), NM_SETTING_WIRELESS_MODE_AP)) return NULL;
    return nm_setting_wireless_get_ssid(sw);
}

/* ── 行为 ── */

static gboolean psk_done(NpSecretRequest *req, gboolean ok, gpointer data)
{
    NpNetwork *n = data;
    if (!ok) {
        np_secret_request_free(req);
        np_network_free(n);
        return TRUE;
    }
    const char *km = np_ap_security_key_mgmt(n->security);
    const char *psk = np_secret_request_get_field(req, NM_SETTING_WIRELESS_SECURITY_PSK)->value;
    GError *e = NULL;
    if (!np_psk_validate(km, psk, &e)) {
        np_page_auth_error(e->message);
        g_error_free(e);
        return FALSE;
    }
    NMConnection *partial = nm_simple_connection_new();
    NMSetting *sw = nm_setting_wireless_new();
    g_object_set(sw, NM_SETTING_WIRELESS_SSID, n->ssid, NULL);
    nm_connection_add_setting(partial, sw);
    NMSetting *ws = nm_setting_wireless_security_new();
    g_object_set(ws, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, km, NM_SETTING_WIRELESS_SECURITY_PSK, psk,
                 NM_SETTING_WIRELESS_SECURITY_PSK_FLAGS, NM_SETTING_SECRET_FLAG_NONE, NULL);
    nm_connection_add_setting(partial, ws);
    np_connect_new(partial, NM_DEVICE(n->device), nm_object_get_path(NM_OBJECT(n->ap)), n->name);
    g_object_unref(partial);
    np_secret_request_free(req);
    np_network_free(n);
    return TRUE;
}

static NpNetwork *net_copy(const NpNetwork *n)
{
    NpNetwork *c = g_new0(NpNetwork, 1);
    *c = *n;
    c->ssid = g_bytes_ref(n->ssid);
    c->name = g_strdup(n->name);
    if (c->ap) g_object_ref(c->ap);
    if (c->device) g_object_ref(c->device);
    if (c->connection) g_object_ref(c->connection);
    return c;
}

static void open_in_editor(const NpNetwork *n)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    np_set_str(&m->id, n->name);
    np_set_str(&m->ssid, n->name);
    m->security = n->security == NP_AP_SEC_WEP ? NP_SEC_WEP : NP_SEC_EAP;
    np_page_editor_open_new(m, NULL, TRUE);
}

static void on_wifi_row(GtkListBox *box, GtkListBoxRow *row, gpointer _)
{
    NpNetwork *n = g_object_get_data(G_OBJECT(row), "net");
    if (!n || n->activating) return;
    if (n->active) {
        np_page_details_open(NM_REMOTE_CONNECTION(n->connection));
        return;
    }
    const char *ap_path = nm_object_get_path(NM_OBJECT(n->ap));
    if (n->connection) {
        np_activate(NM_REMOTE_CONNECTION(n->connection), NM_DEVICE(n->device), ap_path);
        return;
    }
    switch (np_ap_security_quick_connect(n->security)) {
    case NP_QUICK_OPEN:
        np_connect_new(NULL, NM_DEVICE(n->device), ap_path, n->name);
        break;
    case NP_QUICK_PSK:
        np_page_auth_ask(np_secret_request_new_psk(n->name, np_ap_security_label(n->security)), psk_done,
                         net_copy(n));
        break;
    case NP_QUICK_EDITOR:
        open_in_editor(n);
        break;
    }
}

static void on_other_row(GtkListBox *box, GtkListBoxRow *row, gpointer _)
{
    NMRemoteConnection *rc = g_object_get_data(G_OBJECT(row), "conn");
    if (!rc) return;
    if (np_active_for(rc)) np_deactivate(rc);
    else np_activate(rc, NULL, NULL);
}

static void on_details_clicked(NMRemoteConnection *rc)
{
    np_page_details_open(rc);
}

static GtkWidget *details_button(NMRemoteConnection *rc)
{
    GtkWidget *b = np_icon_button(NP_G_CHEV, "详情与设置", NULL, NULL);
    g_signal_connect_data(b, "clicked", G_CALLBACK(on_details_clicked), g_object_ref(rc),
                          (GClosureNotify)(void (*)(void))g_object_unref, G_CONNECT_SWAPPED);
    return b;
}

/* ── 刷新 ── */

static GtkWidget *wifi_row(NpNetwork *n)
{
    GString *s = g_string_new(NULL);
    if (n->active) {
        char *ip = np_ac_ipv4(np_active_for(NM_REMOTE_CONNECTION(n->connection)));
        g_string_append(s, "已连接");
        if (ip) g_string_append_printf(s, " · %s", ip);
        g_free(ip);
    } else if (n->activating) {
        g_string_append(s, "正在连接…");
    } else {
        if (n->connection) g_string_append(s, "已保存 · ");
        g_string_append_printf(s, "%s · %s", np_ap_security_label(n->security),
                               np_band_label(np_band_from_frequency(n->frequency)));
    }
    GtkWidget *row = np_list_row(strength_glyph(n->strength), n->name, s->str, n->active, n->activating,
                                 np_ap_security_is_secure(n->security),
                                 n->connection ? details_button(NM_REMOTE_CONNECTION(n->connection)) : NULL);
    g_string_free(s, TRUE);
    g_object_set_data_full(G_OBJECT(row), "key", g_strdup(n->name), g_free);
    g_object_set_data_full(G_OBJECT(row), "net", n, (GDestroyNotify)np_network_free);
    return row;
}

static GPtrArray *collect_networks(GPtrArray *wdevs)
{
    GArray *in = g_array_new(FALSE, TRUE, sizeof(NpApInput));
    for (guint d = 0; d < wdevs->len; d++) {
        NMDeviceWifi *dev = wdevs->pdata[d];
        NMAccessPoint *active = nm_device_wifi_get_active_access_point(dev);
        const GPtrArray *aps = nm_device_wifi_get_access_points(dev);
        for (guint i = 0; i < aps->len; i++) {
            NMAccessPoint *ap = aps->pdata[i];
            NpApInput x = {
                .ssid = nm_access_point_get_ssid(ap),
                .strength = nm_access_point_get_strength(ap),
                .frequency = nm_access_point_get_frequency(ap),
                .flags = nm_access_point_get_flags(ap),
                .wpa_flags = nm_access_point_get_wpa_flags(ap),
                .rsn_flags = nm_access_point_get_rsn_flags(ap),
                .mode = nm_access_point_get_mode(ap),
                .is_active = ap == active,
                .ap = G_OBJECT(ap),
                .device = G_OBJECT(dev),
            };
            g_array_append_val(in, x);
        }
    }
    GPtrArray *nets = np_networks_from_aps((NpApInput *)in->data, in->len);
    g_array_unref(in);

    /* 当前活动连接 */
    for (guint d = 0; d < wdevs->len; d++) {
        NMActiveConnection *ac = nm_device_get_active_connection(wdevs->pdata[d]);
        NMRemoteConnection *rc = ac ? nm_active_connection_get_connection(ac) : NULL;
        NpNetwork *n = rc ? np_networks_find(nets, conn_ssid(NM_CONNECTION(rc))) : NULL;
        if (!n) continue;
        NMActiveConnectionState st = nm_active_connection_get_state(ac);
        n->active = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATED;
        n->activating = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATING;
        np_network_set_connection(n, rc);
    }
    /* 已保存的连接 */
    const GPtrArray *conns = nm_client_get_connections(np->client);
    for (guint i = 0; i < conns->len; i++) {
        NpNetwork *n = np_networks_find(nets, conn_ssid(conns->pdata[i]));
        if (n && !n->connection) np_network_set_connection(n, conns->pdata[i]);
    }
    np_networks_sort(nets);
    return nets;
}

static gboolean restore_scroll(gpointer _)
{
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(W.scroller), W.restore_scroll);
    return G_SOURCE_REMOVE;
}

static void refocus(const char *key)
{
    GtkListBox *boxes[] = { W.wifi_list, W.other_list };
    for (guint b = 0; b < G_N_ELEMENTS(boxes); b++)
        for (GtkWidget *c = gtk_widget_get_first_child(GTK_WIDGET(boxes[b])); c; c = gtk_widget_get_next_sibling(c))
            if (GTK_IS_LIST_BOX_ROW(c) && !g_strcmp0(g_object_get_data(G_OBJECT(c), "key"), key)) {
                gtk_widget_grab_focus(c);
                return;
            }
}

void np_page_wifi_refresh(void)
{
    if (!np->client) return;
    W.restore_scroll = gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(W.scroller));
    char *focus_key = NULL;
    GtkWidget *fw = gtk_root_get_focus(GTK_ROOT(np->win));
    GtkWidget *frow = fw ? gtk_widget_get_ancestor(fw, GTK_TYPE_LIST_BOX_ROW) : NULL;
    if (frow) focus_key = g_strdup(g_object_get_data(G_OBJECT(frow), "key"));

    np_list_clear(W.wifi_list);
    np_list_clear(W.other_list);

    GPtrArray *wdevs = np_wifi_devices();
    gboolean en = nm_client_wireless_get_enabled(np->client);
    gboolean hw = nm_client_wireless_hardware_get_enabled(np->client);
    W.updating = TRUE;
    gtk_switch_set_active(W.wifi_switch, en);
    W.updating = FALSE;
    gtk_widget_set_sensitive(GTK_WIDGET(W.wifi_switch), hw && wdevs->len);
    gtk_widget_set_sensitive(W.scan_btn, en && wdevs->len);
    gtk_label_set_text(W.placeholder, !nm_client_get_nm_running(np->client) ? "NetworkManager 未运行"
                                      : !wdevs->len                          ? "未检测到无线网卡"
                                      : !hw                                  ? "无线已被硬件开关禁用"
                                      : !en                                  ? "Wi-Fi 已关闭"
                                                                             : "正在搜索网络…");
    if (en && wdevs->len) {
        GPtrArray *nets = collect_networks(wdevs);
        /* 行接管 NpNetwork 的所有权 */
        for (guint i = 0; i < nets->len; i++) gtk_list_box_append(W.wifi_list, wifi_row(nets->pdata[i]));
        g_ptr_array_set_free_func(nets, NULL);
        g_ptr_array_unref(nets);
    }
    g_ptr_array_unref(wdevs);

    /* 有线 / VPN / WireGuard 快捷切换 */
    int others = 0;
    const GPtrArray *conns = nm_client_get_connections(np->client);
    for (guint i = 0; i < conns->len; i++) {
        NMRemoteConnection *rc = conns->pdata[i];
        const char *t = nm_connection_get_connection_type(NM_CONNECTION(rc));
        if (g_strcmp0(t, NM_SETTING_WIRED_SETTING_NAME) && g_strcmp0(t, NM_SETTING_VPN_SETTING_NAME) &&
            g_strcmp0(t, NM_SETTING_WIREGUARD_SETTING_NAME))
            continue;
        NMActiveConnection *ac = np_active_for(rc);
        NMActiveConnectionState st = ac ? nm_active_connection_get_state(ac) : NM_ACTIVE_CONNECTION_STATE_UNKNOWN;
        gboolean on = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATED;
        gboolean busy = st == NM_ACTIVE_CONNECTION_STATE_ACTIVATING || st == NM_ACTIVE_CONNECTION_STATE_DEACTIVATING;
        char *ip = on ? np_ac_ipv4(ac) : NULL;
        char *sub = on ? g_strdup_printf("已连接%s%s", ip ? " · " : "", ip ? ip : "")
                       : g_strdup(busy ? np_ac_state_label(ac) : "未连接 · 点击连接");
        GtkWidget *row = np_list_row(np_conn_glyph(NM_CONNECTION(rc)), nm_connection_get_id(NM_CONNECTION(rc)), sub,
                                     on, busy, FALSE, details_button(rc));
        g_object_set_data_full(G_OBJECT(row), "key", g_strdup(nm_object_get_path(NM_OBJECT(rc))), g_free);
        g_object_set_data_full(G_OBJECT(row), "conn", g_object_ref(rc), g_object_unref);
        gtk_list_box_append(W.other_list, row);
        g_free(ip);
        g_free(sub);
        others++;
    }
    gtk_widget_set_visible(W.other_section, others > 0);

    if (focus_key) refocus(focus_key);
    g_free(focus_key);
    g_idle_add(restore_scroll, NULL);
}

/* ── 扫描 / 开关 ── */

static gboolean scan_anim_stop(gpointer _)
{
    W.scan_anim = 0;
    gtk_widget_set_visible(W.scan_spin, FALSE);
    gtk_widget_set_visible(W.scan_glyph, TRUE);
    return G_SOURCE_REMOVE;
}

void np_page_wifi_scan_done(void)
{
    if (!W.scan_anim) return;
    g_source_remove(W.scan_anim);
    scan_anim_stop(NULL);
}

static void on_scan(void)
{
    np_request_scan(0);
    gtk_widget_set_visible(W.scan_glyph, FALSE);
    gtk_widget_set_visible(W.scan_spin, TRUE);
    if (W.scan_anim) g_source_remove(W.scan_anim);
    W.scan_anim = g_timeout_add_seconds(8, scan_anim_stop, NULL);
}

static void on_wifi_switch(GObject *sw, GParamSpec *ps, gpointer _)
{
    if (!W.updating) np_set_wireless_enabled(gtk_switch_get_active(GTK_SWITCH(sw)));
}

static void on_hidden(void)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    np_set_str(&m->id, "");
    m->hidden = TRUE;
    np_page_editor_open_new(m, NULL, TRUE);
}

static void on_settings(void)
{
    np_nav_push(NP_PAGE_CONNECTIONS);
}

GtkWidget *np_page_wifi_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *h = np_header("Wi-Fi", NULL, FALSE);
    W.scan_btn = gtk_button_new();
    GtkWidget *sb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    W.scan_glyph = np_glyph(NP_G_REFRESH, NULL);
    W.scan_spin = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(W.scan_spin));
    gtk_widget_set_visible(W.scan_spin, FALSE);
    gtk_box_append(GTK_BOX(sb), W.scan_glyph);
    gtk_box_append(GTK_BOX(sb), W.scan_spin);
    gtk_button_set_child(GTK_BUTTON(W.scan_btn), sb);
    gtk_widget_add_css_class(W.scan_btn, "icon");
    gtk_widget_set_valign(W.scan_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(W.scan_btn, "重新扫描");
    g_signal_connect_swapped(W.scan_btn, "clicked", G_CALLBACK(on_scan), NULL);
    gtk_box_append(GTK_BOX(h), W.scan_btn);

    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(sw, 4);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_wifi_switch), NULL);
    W.wifi_switch = GTK_SWITCH(sw);
    gtk_box_append(GTK_BOX(h), sw);
    gtk_box_append(GTK_BOX(page), h);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    W.wifi_list = np_list_new();
    W.placeholder = GTK_LABEL(np_label("正在连接 NetworkManager…", "placeholder", 0.5));
    gtk_list_box_set_placeholder(W.wifi_list, GTK_WIDGET(W.placeholder));
    g_signal_connect(W.wifi_list, "row-activated", G_CALLBACK(on_wifi_row), NULL);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(W.wifi_list));

    W.other_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(W.other_section), np_label("其他连接", "section", 0));
    W.other_list = np_list_new();
    g_signal_connect(W.other_list, "row-activated", G_CALLBACK(on_other_row), NULL);
    gtk_box_append(GTK_BOX(W.other_section), GTK_WIDGET(W.other_list));
    gtk_widget_set_visible(W.other_section, FALSE);
    gtk_box_append(GTK_BOX(content), W.other_section);

    GtkWidget *sc = np_scroller(content, 440);
    W.scroller = GTK_SCROLLED_WINDOW(sc);
    gtk_box_append(GTK_BOX(page), sc);

    GtkWidget *foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(foot, "footer");
    gtk_box_append(GTK_BOX(foot), np_glyph_button(NP_G_SETTINGS, "网络设置", "flat", G_CALLBACK(on_settings), NULL));
    gtk_box_append(GTK_BOX(foot), np_spacer());
    gtk_box_append(GTK_BOX(foot), np_glyph_button(NP_G_HIDDEN, "隐藏网络", "flat", G_CALLBACK(on_hidden), NULL));
    gtk_box_append(GTK_BOX(page), foot);
    return page;
}
