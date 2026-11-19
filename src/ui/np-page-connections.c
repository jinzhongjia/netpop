// SPDX-License-Identifier: GPL-3.0-or-later
/* “网络设置”：全部连接（按类型分组）、全局开关；“新建连接”：模板与导入 */
#include "np-actions.h"
#include "np-app.h"
#include "np-pages.h"
#include "np-widgets.h"
#include "np-wg.h"

#include <sys/random.h>

static struct {
    GtkWidget *groups;
    GtkSwitch *networking, *wifi;
    gboolean updating;
} C;

static struct {
    GtkListBox *vpn_list;
    GSList *plugins; /* NMVpnPluginInfo* */
} N;

/* ───────────── 全部连接 ───────────── */

static const char *group_of(NMConnection *c)
{
    const char *t = np_conn_type_label(c);
    if (!g_strcmp0(t, "Wi-Fi") || !g_strcmp0(t, "热点") || !g_strcmp0(t, "有线") || !g_strcmp0(t, "VPN") ||
        !g_strcmp0(t, "WireGuard"))
        return t;
    return "其他";
}

static gint by_name(gconstpointer a, gconstpointer b)
{
    NMConnection *x = *(NMConnection *const *)a, *y = *(NMConnection *const *)b;
    return g_utf8_collate(nm_connection_get_id(x), nm_connection_get_id(y));
}

static void on_conn_row(GtkListBox *box, GtkListBoxRow *row, gpointer _)
{
    NMRemoteConnection *rc = g_object_get_data(G_OBJECT(row), "conn");
    if (rc) np_page_details_open(rc);
}

void np_page_connections_refresh(void)
{
    if (!np->client || !C.groups) return;
    C.updating = TRUE;
    gtk_switch_set_active(C.networking, nm_client_networking_get_enabled(np->client));
    gtk_switch_set_active(C.wifi, nm_client_wireless_get_enabled(np->client));
    gtk_widget_set_sensitive(GTK_WIDGET(C.wifi), nm_client_wireless_hardware_get_enabled(np->client));
    C.updating = FALSE;

    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(C.groups))) gtk_box_remove(GTK_BOX(C.groups), c);

    static const char *const order[] = { "Wi-Fi", "热点", "有线", "VPN", "WireGuard", "其他" };
    GPtrArray *conns = g_ptr_array_new();
    const GPtrArray *all = nm_client_get_connections(np->client);
    for (guint i = 0; i < all->len; i++) {
        const char *t = nm_connection_get_connection_type(all->pdata[i]);
        if (g_strcmp0(t, NM_SETTING_LOOPBACK_SETTING_NAME)) g_ptr_array_add(conns, all->pdata[i]);
    }
    g_ptr_array_sort(conns, by_name);

    for (guint g = 0; g < G_N_ELEMENTS(order); g++) {
        GtkListBox *list = NULL;
        for (guint i = 0; i < conns->len; i++) {
            NMRemoteConnection *rc = conns->pdata[i];
            NMConnection *conn = NM_CONNECTION(rc);
            if (g_strcmp0(group_of(conn), order[g])) continue;
            if (!list) {
                gtk_box_append(GTK_BOX(C.groups), np_label(order[g], "section", 0));
                list = np_list_new();
                g_signal_connect(list, "row-activated", G_CALLBACK(on_conn_row), NULL);
                gtk_box_append(GTK_BOX(C.groups), GTK_WIDGET(list));
            }
            NMActiveConnection *ac = np_active_for(rc);
            NMSettingConnection *sc = nm_connection_get_setting_connection(conn);
            char *sub = g_strdup_printf("%s%s%s", np_ac_state_label(ac),
                                        !g_strcmp0(order[g], "其他") ? " · " : "",
                                        !g_strcmp0(order[g], "其他") ? np_conn_type_label(conn) : "");
            if (sc && !nm_setting_connection_get_autoconnect(sc)) {
                char *t = g_strconcat(sub, " · 手动连接", NULL);
                g_free(sub);
                sub = t;
            }
            gboolean busy = ac && nm_active_connection_get_state(ac) == NM_ACTIVE_CONNECTION_STATE_ACTIVATING;
            GtkWidget *row = np_list_row(np_conn_glyph(conn), nm_connection_get_id(conn), sub,
                                         ac && !busy, busy, FALSE, np_glyph(NP_G_CHEV, "chev"));
            g_object_set_data_full(G_OBJECT(row), "conn", g_object_ref(rc), g_object_unref);
            gtk_list_box_append(list, row);
            g_free(sub);
        }
    }
    if (!conns->len) gtk_box_append(GTK_BOX(C.groups), np_label("还没有保存的连接", "placeholder", 0.5));
    g_ptr_array_unref(conns);
}

static void on_networking(GObject *sw, GParamSpec *ps, gpointer _)
{
    if (!C.updating) np_set_networking_enabled(gtk_switch_get_active(GTK_SWITCH(sw)));
}

static void on_wifi(GObject *sw, GParamSpec *ps, gpointer _)
{
    if (!C.updating) np_set_wireless_enabled(gtk_switch_get_active(GTK_SWITCH(sw)));
}

static void on_new(void)
{
    np_nav_push(NP_PAGE_NEW);
}

GtkWidget *np_page_connections_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *h = np_header("网络设置", NULL, TRUE);
    gtk_box_append(GTK_BOX(h), np_icon_button(NP_G_PLUS, "新建连接", G_CALLBACK(on_new), NULL));
    gtk_box_append(GTK_BOX(page), h);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *card = np_section(body, NULL);
    C.networking = np_form_switch(card, "启用网络", "关闭后断开所有连接（类似飞行模式）", TRUE);
    g_signal_connect(C.networking, "notify::active", G_CALLBACK(on_networking), NULL);
    C.wifi = np_form_switch(card, "Wi-Fi", NULL, TRUE);
    g_signal_connect(C.wifi, "notify::active", G_CALLBACK(on_wifi), NULL);

    C.groups = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(body), C.groups);
    gtk_box_append(GTK_BOX(page), np_scroller(body, 520));
    return page;
}

/* ───────────── 新建连接 ───────────── */

static char *random_password(void)
{
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    guint8 buf[12];
    gsize got = 0;
    while (got < sizeof buf) {
        ssize_t n = getrandom(buf + got, sizeof buf - got, 0);
        if (n > 0) got += (gsize)n;
    }
    char *s = g_malloc(sizeof buf + 1);
    for (gsize i = 0; i < sizeof buf; i++) s[i] = alphabet[buf[i] % (sizeof alphabet - 1)];
    s[sizeof buf] = '\0';
    return s;
}

static void new_wifi(void)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_WIRELESS_SETTING_NAME);
    np_set_str(&m->id, "");
    np_page_editor_open_new(m, NULL, TRUE);
}

static void new_hotspot(void)
{
    char *ssid = g_strdup_printf("%s-hotspot", g_get_host_name());
    char *pw = random_password();
    NpConnModel *m = np_conn_model_new_hotspot(ssid, pw);
    np_page_editor_open_new(m, NULL, TRUE);
    g_free(ssid);
    g_free(pw);
}

static void new_wired(void)
{
    np_page_editor_open_new(np_conn_model_new(NM_SETTING_WIRED_SETTING_NAME), NULL, FALSE);
}

static void new_wireguard(void)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_WIREGUARD_SETTING_NAME);
    char *k = np_wg_generate_private_key();
    np_set_str(&m->wg_private_key, k);
    g_free(k);
    np_page_editor_open_new(m, NULL, FALSE);
}

typedef void (*ImportFn)(const char *path);

static void import_chosen(GObject *src, GAsyncResult *res, gpointer data)
{
    ImportFn fn = (ImportFn)data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    np_modal_end();
    if (!f) return;
    char *path = g_file_get_path(f);
    if (path) fn(path);
    g_free(path);
    g_object_unref(f);
}

static void choose_file(const char *title, const char *pattern, ImportFn fn)
{
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, title);
    if (pattern) {
        GtkFileFilter *flt = gtk_file_filter_new();
        gtk_file_filter_add_pattern(flt, pattern);
        gtk_file_filter_set_name(flt, pattern);
        GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
        g_list_store_append(store, flt);
        gtk_file_dialog_set_filters(d, G_LIST_MODEL(store));
        g_object_unref(store);
        g_object_unref(flt);
    }
    np_modal_begin();
    gtk_file_dialog_open(d, np->win, NULL, import_chosen, (gpointer)fn);
    g_object_unref(d);
}

static void open_imported(NMConnection *c)
{
    NpConnModel *m = np_conn_model_from_connection(c);
    np_page_editor_open_new(m, c, FALSE);
}

static void import_wg_file(const char *path)
{
    GError *e = NULL;
    NMConnection *c = np_wg_import(path, &e);
    if (!c) {
        np_notify("导入失败", "%s", e->message);
        g_error_free(e);
        return;
    }
    open_imported(c);
}

static void import_vpn_file(const char *path)
{
    GError *last = NULL;
    for (GSList *l = N.plugins; l; l = l->next) {
        GError *e = NULL;
        NMVpnEditorPlugin *p = nm_vpn_plugin_info_load_editor_plugin(l->data, &e);
        NMConnection *c = p ? nm_vpn_editor_plugin_import(p, path, &e) : NULL;
        if (c) {
            g_clear_error(&last);
            open_imported(c);
            return;
        }
        g_clear_error(&last);
        last = e;
    }
    np_notify("导入失败", "%s", last ? last->message : "没有可以识别该文件的 VPN 插件");
    g_clear_error(&last);
}

static void import_wg(void)
{
    choose_file("导入 WireGuard 配置", "*.conf", import_wg_file);
}

static void import_vpn(void)
{
    if (!N.plugins) {
        np_notify(NULL, "未安装 VPN 插件（例如 networkmanager-openvpn）");
        return;
    }
    choose_file("导入 VPN 配置", NULL, import_vpn_file);
}

static void new_vpn(NMVpnPluginInfo *info)
{
    NpConnModel *m = np_conn_model_new(NM_SETTING_VPN_SETTING_NAME);
    np_set_str(&m->id, nm_vpn_plugin_info_get_name(info));
    np_set_str(&m->vpn_service, nm_vpn_plugin_info_get_service(info));
    np_page_editor_open_new(m, NULL, FALSE);
}

typedef struct {
    const char *glyph, *title, *sub;
    void (*fn)(void);
} Template;

static const Template templates[] = {
    { NP_G_WIFI, "Wi-Fi 网络", "手动添加，或连接隐藏网络", new_wifi },
    { NP_G_HOTSPOT, "个人热点", "通过 Wi-Fi 共享本机网络", new_hotspot },
    { NP_G_ETHERNET, "有线连接", "静态 IP、MAC 等有线配置", new_wired },
    { NP_G_VPN, "WireGuard", "新建隧道并生成密钥", new_wireguard },
    { NP_G_IMPORT, "导入 WireGuard 配置", "wg-quick 格式的 .conf 文件", import_wg },
    { NP_G_IMPORT, "导入 VPN 配置", "由已安装的 VPN 插件识别（如 .ovpn）", import_vpn },
};

static void on_template(GtkListBox *box, GtkListBoxRow *row, gpointer _)
{
    const Template *t = g_object_get_data(G_OBJECT(row), "tpl");
    NMVpnPluginInfo *info = g_object_get_data(G_OBJECT(row), "plugin");
    if (t) t->fn();
    else if (info) new_vpn(info);
}

GtkWidget *np_page_new_new(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(page), np_header("新建连接", NULL, TRUE));

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkListBox *list = np_list_new();
    g_signal_connect(list, "row-activated", G_CALLBACK(on_template), NULL);
    for (guint i = 0; i < G_N_ELEMENTS(templates); i++) {
        GtkWidget *row = np_list_row(templates[i].glyph, templates[i].title, templates[i].sub, FALSE, FALSE, FALSE,
                                     np_glyph(NP_G_CHEV, "chev"));
        g_object_set_data(G_OBJECT(row), "tpl", (gpointer)&templates[i]);
        gtk_list_box_append(list, row);
    }
    gtk_box_append(GTK_BOX(body), GTK_WIDGET(list));

    /* 已安装的 VPN 插件 */
    N.plugins = nm_vpn_plugin_info_list_load();
    gtk_box_append(GTK_BOX(body), np_label("VPN", "section", 0));
    N.vpn_list = np_list_new();
    g_signal_connect(N.vpn_list, "row-activated", G_CALLBACK(on_template), NULL);
    for (GSList *l = N.plugins; l; l = l->next) {
        NMVpnPluginInfo *info = l->data;
        GtkWidget *row = np_list_row(NP_G_VPN, nm_vpn_plugin_info_get_name(info), nm_vpn_plugin_info_get_service(info),
                                     FALSE, FALSE, FALSE, np_glyph(NP_G_CHEV, "chev"));
        g_object_set_data(G_OBJECT(row), "plugin", info);
        gtk_list_box_append(N.vpn_list, row);
    }
    GtkWidget *ph = np_label("未安装 VPN 插件。安装 networkmanager-openvpn 等软件包后，可在此新建对应类型的 VPN。",
                             "placeholder-small", 0);
    gtk_label_set_wrap(GTK_LABEL(ph), TRUE);
    gtk_list_box_set_placeholder(N.vpn_list, ph);
    gtk_box_append(GTK_BOX(body), GTK_WIDGET(N.vpn_list));
    gtk_box_append(GTK_BOX(page), np_scroller(body, 520));
    return page;
}
