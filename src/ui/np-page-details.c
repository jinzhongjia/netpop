// SPDX-License-Identifier: GPL-3.0-or-later
/* 连接详情：运行状态 + 编辑 / 连接 / 断开 / 删除 */
#include "np-actions.h"
#include "np-app.h"
#include "np-pages.h"
#include "np-widgets.h"

#include <sys/socket.h>

static struct {
    NMRemoteConnection *rc;
    GtkLabel *title;
    GtkGrid *grid;
    GtkWidget *toggle, *forget;
    guint forget_armed;
} D;

static void info_row(const char *k, const char *v, int *r)
{
    if (!v || !*v) return;
    GtkWidget *kl = np_label(k, "key", 0);
    gtk_widget_set_valign(kl, GTK_ALIGN_START);
    GtkWidget *vl = np_label(v, "val", 0);
    gtk_label_set_selectable(GTK_LABEL(vl), TRUE);
    gtk_label_set_wrap(GTK_LABEL(vl), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(vl), PANGO_WRAP_WORD_CHAR);
    gtk_widget_set_hexpand(vl, TRUE);
    gtk_widget_set_focusable(vl, FALSE);
    gtk_grid_attach(D.grid, kl, 0, *r, 1, 1);
    gtk_grid_attach(D.grid, vl, 1, *r, 1, 1);
    (*r)++;
}

static char *join_addrs(NMIPConfig *cfg, gboolean skip_link_local)
{
    GString *s = g_string_new(NULL);
    GPtrArray *a = cfg ? nm_ip_config_get_addresses(cfg) : NULL;
    for (guint i = 0; a && i < a->len; i++) {
        const char *ip = nm_ip_address_get_address(a->pdata[i]);
        if (skip_link_local && g_str_has_prefix(ip, "fe80")) continue;
        g_string_append_printf(s, "%s%s/%u", s->len ? "\n" : "", ip, nm_ip_address_get_prefix(a->pdata[i]));
    }
    return g_string_free(s, FALSE);
}

static void fill(void)
{
    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(GTK_WIDGET(D.grid)))) gtk_grid_remove(D.grid, c);
    if (!D.rc) return;
    NMConnection *conn = NM_CONNECTION(D.rc);
    NMActiveConnection *ac = np_active_for(D.rc);
    int r = 0;

    gtk_label_set_text(D.title, nm_connection_get_id(conn));
    gtk_button_set_label(GTK_BUTTON(D.toggle), ac ? "断开" : "连接");
    info_row("类型", np_conn_type_label(conn), &r);
    info_row("状态", np_ac_state_label(ac), &r);

    NMSettingConnection *sc = nm_connection_get_setting_connection(conn);
    info_row("自动连接", sc && nm_setting_connection_get_autoconnect(sc) ? "是" : "否", &r);
    if (!ac) {
        info_row("绑定设备", nm_connection_get_interface_name(conn), &r);
        return;
    }

    NMDevice *dev = np_ac_device(ac);
    if (NM_IS_DEVICE_WIFI(dev)) {
        NMAccessPoint *ap = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(dev));
        if (ap) {
            guint32 f = nm_access_point_get_frequency(ap);
            NpApSecurity sec = np_ap_security_classify(nm_access_point_get_flags(ap), nm_access_point_get_wpa_flags(ap),
                                                       nm_access_point_get_rsn_flags(ap));
            char *sig = g_strdup_printf("%u%%", nm_access_point_get_strength(ap));
            char *fr = g_strdup_printf("%s · 信道 %u", np_band_label(np_band_from_frequency(f)),
                                       nm_utils_wifi_freq_to_channel(f));
            char *rate = g_strdup_printf("%u Mb/s", nm_device_wifi_get_bitrate(NM_DEVICE_WIFI(dev)) / 1000);
            info_row("信号", sig, &r);
            info_row("频段", fr, &r);
            info_row("速率", rate, &r);
            info_row("安全", np_ap_security_label(sec), &r);
            info_row("BSSID", nm_access_point_get_bssid(ap), &r);
            g_free(sig);
            g_free(fr);
            g_free(rate);
        }
    } else if (NM_IS_DEVICE_ETHERNET(dev)) {
        guint32 speed = nm_device_ethernet_get_speed(NM_DEVICE_ETHERNET(dev));
        char *sp = speed ? g_strdup_printf("%u Mb/s", speed) : NULL;
        info_row("速率", sp, &r);
        g_free(sp);
    }

    NMIPConfig *ip4 = nm_active_connection_get_ip4_config(ac);
    char *a4 = join_addrs(ip4, FALSE);
    info_row("IPv4", a4, &r);
    g_free(a4);
    if (ip4) {
        info_row("网关", nm_ip_config_get_gateway(ip4), &r);
        const char *const *ns = nm_ip_config_get_nameservers(ip4);
        char *dns = ns ? g_strjoinv(", ", (char **)ns) : NULL;
        info_row("DNS", dns, &r);
        g_free(dns);
    }
    char *a6 = join_addrs(nm_active_connection_get_ip6_config(ac), TRUE);
    info_row("IPv6", a6, &r);
    g_free(a6);
    if (dev) {
        info_row("接口", nm_device_get_iface(dev), &r);
        info_row("MAC", nm_device_get_hw_address(dev), &r);
    }
}

void np_page_details_open(NMRemoteConnection *rc)
{
    if (!rc) return;
    g_set_object(&D.rc, rc);
    if (D.forget_armed) {
        g_source_remove(D.forget_armed);
        D.forget_armed = 0;
    }
    gtk_button_set_label(GTK_BUTTON(D.forget), "删除");
    fill();
    np_nav_push(NP_PAGE_DETAILS);
}

void np_page_details_refresh(void)
{
    if (!D.rc) return;
    const GPtrArray *conns = nm_client_get_connections(np->client);
    gboolean exists = FALSE;
    for (guint i = 0; i < conns->len && !exists; i++) exists = conns->pdata[i] == (gpointer)D.rc;
    if (!exists) {
        if (!g_strcmp0(np_nav_current(), NP_PAGE_DETAILS)) np_nav_back();
        return;
    }
    fill();
}

void np_page_details_leave(void)
{
    if (D.forget_armed) {
        g_source_remove(D.forget_armed);
        D.forget_armed = 0;
    }
    g_clear_object(&D.rc);
}

static void on_toggle(void)
{
    if (!D.rc) return;
    if (np_active_for(D.rc)) np_deactivate(D.rc);
    else np_activate(D.rc, NULL, NULL);
}

static void on_edit(void)
{
    if (D.rc) np_page_editor_open(D.rc);
}

static gboolean forget_disarm(gpointer _)
{
    D.forget_armed = 0;
    gtk_button_set_label(GTK_BUTTON(D.forget), "删除");
    return G_SOURCE_REMOVE;
}

static void on_forget(void)
{
    if (!D.rc) return;
    if (!D.forget_armed) { /* 二次确认 */
        gtk_button_set_label(GTK_BUTTON(D.forget), "确认删除？");
        D.forget_armed = g_timeout_add_seconds(3, forget_disarm, NULL);
        return;
    }
    g_source_remove(D.forget_armed);
    D.forget_armed = 0;
    np_forget(D.rc);
    np_nav_back();
}

GtkWidget *np_page_details_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(page), np_header("", &D.title, TRUE));

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *card = gtk_grid_new();
    D.grid = GTK_GRID(card);
    gtk_grid_set_row_spacing(D.grid, 5);
    gtk_grid_set_column_spacing(D.grid, 14);
    gtk_widget_add_css_class(card, "card");
    gtk_box_append(GTK_BOX(body), card);
    gtk_box_append(GTK_BOX(page), np_scroller(body, 520));

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(btns, "footer");
    D.forget = np_button("删除", "destructive", G_CALLBACK(on_forget), NULL);
    gtk_box_append(GTK_BOX(btns), D.forget);
    gtk_box_append(GTK_BOX(btns), np_spacer());
    gtk_box_append(GTK_BOX(btns), np_button("编辑", NULL, G_CALLBACK(on_edit), NULL));
    D.toggle = np_button("连接", "suggested", G_CALLBACK(on_toggle), NULL);
    gtk_box_append(GTK_BOX(btns), D.toggle);
    gtk_box_append(GTK_BOX(page), btns);
    return page;
}
