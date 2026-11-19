// SPDX-License-Identifier: GPL-3.0-or-later
/* netpop — NetworkManager 网络面板
 *
 *   netpop              切换显示 / 隐藏（首次运行会常驻）
 *   netpop --show | --hide | --daemon | --quit
 *   netpop --settings   直接打开“网络设置”
 *   netpop --new        打开“新建连接”
 *   netpop --edit ID    编辑指定连接（UUID 或名称），相当于 nm-connection-editor --edit
 */
#include "config.h"
#include "np-app.h"
#include "np-pages.h"

#include <stdio.h>

/* arg_data 必须为 NULL：否则 GLib 把值写进变量而不放进 options dict，
 * 常驻实例就收不到远程调用传来的参数。 */
static const GOptionEntry entries[] = {
    { "show", 0, 0, G_OPTION_ARG_NONE, NULL, "显示面板", NULL },
    { "hide", 0, 0, G_OPTION_ARG_NONE, NULL, "隐藏面板", NULL },
    { "settings", 0, 0, G_OPTION_ARG_NONE, NULL, "打开“网络设置”页", NULL },
    { "new", 0, 0, G_OPTION_ARG_NONE, NULL, "打开“新建连接”页", NULL },
    { "edit", 0, 0, G_OPTION_ARG_STRING, NULL, "编辑连接（UUID 或名称）", "ID" },
    { "daemon", 0, 0, G_OPTION_ARG_NONE, NULL, "仅在后台启动（用于自启动）", NULL },
    { "quit", 0, 0, G_OPTION_ARG_NONE, NULL, "退出常驻进程", NULL },
    { "version", 0, 0, G_OPTION_ARG_NONE, NULL, "显示版本", NULL },
    { NULL },
};

static int on_local_options(GApplication *app, GVariantDict *opts, gpointer _)
{
    if (g_variant_dict_contains(opts, "version")) {
        printf("netpop %s\n", NP_VERSION);
        return 0;
    }
    return -1;
}

static gboolean has(GVariantDict *d, const char *key)
{
    gboolean v = FALSE;
    return g_variant_dict_lookup(d, key, "b", &v) && v;
}

static int on_command_line(GApplication *app, GApplicationCommandLine *cl, gpointer _)
{
    GVariantDict *d = g_application_command_line_get_options_dict(cl);
    const char *edit = NULL;
    if (has(d, "quit")) {
        g_application_release(app);
        g_application_quit(app);
    } else if (has(d, "daemon")) {
        /* 只保持常驻 */
    } else if (has(d, "hide")) {
        np_popup_hide();
    } else if (has(d, "settings") || has(d, "new")) {
        np_popup_show();
        np_nav_home();
        np_nav_push(NP_PAGE_CONNECTIONS);
        if (has(d, "new")) np_nav_push(NP_PAGE_NEW);
    } else if (g_variant_dict_lookup(d, "edit", "&s", &edit)) {
        NMRemoteConnection *rc = np->client ? nm_client_get_connection_by_uuid(np->client, edit) : NULL;
        const GPtrArray *all = np->client ? nm_client_get_connections(np->client) : NULL;
        for (guint i = 0; !rc && all && i < all->len; i++)
            if (!g_strcmp0(nm_connection_get_id(all->pdata[i]), edit)) rc = all->pdata[i];
        if (!rc) {
            g_application_command_line_printerr(cl, "找不到连接：%s\n", edit);
            return 1;
        }
        np_popup_show();
        np_nav_home();
        np_nav_push(NP_PAGE_CONNECTIONS);
        np_page_editor_open(rc);
    } else if (has(d, "show")) {
        np_popup_show();
    } else {
        np_popup_toggle();
    }
    return 0;
}

static void on_startup(GApplication *app, gpointer _)
{
    g_application_hold(app); /* 常驻：再次打开是瞬时的 */
    np_app_startup(GTK_APPLICATION(app));
}

static void on_shutdown(GApplication *app, gpointer _)
{
    np_app_shutdown();
}

int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new(NP_APP_ID, G_APPLICATION_HANDLES_COMMAND_LINE);
    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_application_set_option_context_summary(G_APPLICATION(app), "NetworkManager 网络面板");
    g_signal_connect(app, "handle-local-options", G_CALLBACK(on_local_options), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), NULL);
    int r = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return r;
}
