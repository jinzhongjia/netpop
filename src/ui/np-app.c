#include "np-app.h"
#include "np-actions.h"
#include "np-pages.h"

#include <stdarg.h>
#if HAVE_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif

#define RES_PREFIX "/dev/netpop/Netpop/"

static NpApp app_state;
NpApp *np = &app_state;

/* ───────────── 样式 ───────────── */

static char *read_resource(const char *name)
{
    char *path = g_strconcat(RES_PREFIX, name, NULL);
    GBytes *b = g_resources_lookup_data(path, 0, NULL);
    g_free(path);
    if (!b) return g_strdup("");
    char *s = g_strndup(g_bytes_get_data(b, NULL), g_bytes_get_size(b));
    g_bytes_unref(b);
    return s;
}

static char *read_file(const char *path)
{
    char *s = NULL;
    if (path) g_file_get_contents(path, &s, NULL, NULL);
    return s ? s : g_strdup("");
}

/* 每次弹出时重新读取颜色与自定义样式文件，内容不变则不重新解析 */
static void load_css(void)
{
    char *palette = read_resource("palette.css");
    char *colors = read_file(np->config->colors_css);
    char *style = read_resource("style.css");
    char *user = read_file(np->config->style_css);
    char *css = g_strjoin("\n", palette, colors, style, user, NULL);
    if (g_strcmp0(css, np->css_cache)) {
        gtk_css_provider_load_from_string(np->css, css);
        g_free(np->css_cache);
        np->css_cache = css;
    } else {
        g_free(css);
    }
    g_free(palette);
    g_free(colors);
    g_free(style);
    g_free(user);
}

/* ───────────── 通知 ───────────── */

static gboolean toast_hide(gpointer _)
{
    np->toast_id = 0;
    gtk_revealer_set_reveal_child(np->toast_rev, FALSE);
    return G_SOURCE_REMOVE;
}

void np_notify(const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *body = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    if (np_popup_visible()) {
        char *t = title ? g_strdup_printf("%s · %s", title, body) : g_strdup(body);
        gtk_label_set_text(np->toast, t);
        gtk_revealer_set_reveal_child(np->toast_rev, TRUE);
        if (np->toast_id) g_source_remove(np->toast_id);
        np->toast_id = g_timeout_add(3000, toast_hide, NULL);
        g_free(t);
    } else if (title) {
        GNotification *n = g_notification_new(title);
        g_notification_set_body(n, body);
        g_application_send_notification(G_APPLICATION(np->app), "netpop", n);
        g_object_unref(n);
    }
    g_free(body);
}

/* ───────────── 刷新 ───────────── */

static void refresh_all(void)
{
    np->dirty = FALSE;
    if (!np->client) return;
    np_page_wifi_refresh();
    np_page_connections_refresh();
    np_page_details_refresh();
}

static gboolean refresh_cb(gpointer _)
{
    np->refresh_id = 0;
    refresh_all();
    return G_SOURCE_REMOVE;
}

void np_queue_refresh(void)
{
    if (!np_popup_visible()) {
        np->dirty = TRUE; /* 隐藏时不做任何界面工作 */
        return;
    }
    if (!np->refresh_id) np->refresh_id = g_timeout_add(80, refresh_cb, NULL);
}

void np_modal_begin(void) { np->modal++; }
void np_modal_end(void) { if (np->modal > 0) np->modal--; }

/* ───────────── 导航 ───────────── */

const char *np_nav_current(void)
{
    return gtk_stack_get_visible_child_name(np->stack);
}

static void leave(const char *page)
{
    if (!g_strcmp0(page, NP_PAGE_AUTH)) np_page_auth_cancel();
    else if (!g_strcmp0(page, NP_PAGE_DETAILS)) np_page_details_leave();
    else if (!g_strcmp0(page, NP_PAGE_EDITOR)) np_page_editor_leave();
}

static void show_page(const char *page)
{
    gtk_stack_set_visible_child_name(np->stack, page);
    if (!g_strcmp0(page, NP_PAGE_CONNECTIONS)) np_page_connections_refresh();
    else if (!g_strcmp0(page, NP_PAGE_WIFI) && np->dirty) refresh_all();
}

void np_nav_push(const char *page)
{
    const char *cur = np_nav_current();
    if (!g_strcmp0(cur, page)) return;
    g_ptr_array_add(np->nav, g_strdup(cur));
    show_page(page);
}

void np_nav_back(void)
{
    char *cur = g_strdup(np_nav_current());
    if (np->nav->len == 0) {
        g_free(cur);
        np_popup_hide();
        return;
    }
    char *prev = g_ptr_array_steal_index(np->nav, np->nav->len - 1);
    show_page(prev);
    leave(cur);
    g_free(prev);
    g_free(cur);
}

void np_nav_home(void)
{
    char *cur = g_strdup(np_nav_current());
    GPtrArray *pages = np->nav;
    np->nav = g_ptr_array_new_with_free_func(g_free);
    show_page(NP_PAGE_WIFI);
    leave(cur);
    for (guint i = 0; i < pages->len; i++) leave(pages->pdata[i]);
    g_ptr_array_unref(pages);
    g_free(cur);
}

/* ───────────── 弹出 / 收起 ───────────── */

gboolean np_popup_visible(void)
{
    return np->win && gtk_widget_get_visible(GTK_WIDGET(np->win));
}

void np_popup_show(void)
{
    load_css();
    if (np->refresh_id) {
        g_source_remove(np->refresh_id);
        np->refresh_id = 0;
        np->dirty = TRUE;
    }
    np->shown_at = g_get_monotonic_time();
    np->was_active = FALSE;
    gtk_window_present(np->win);
    if (np->dirty) refresh_all();
    if (np->client) np_request_scan(15000);
}

void np_popup_hide(void)
{
    if (!np_popup_visible()) return;
    np_nav_home();
    gtk_widget_set_visible(GTK_WIDGET(np->win), FALSE);
}

void np_popup_toggle(void)
{
    if (np_popup_visible()) np_popup_hide();
    else np_popup_show();
}

static gboolean focus_in_popover(void)
{
    GtkWidget *f = gtk_root_get_focus(GTK_ROOT(np->win));
    return f && gtk_widget_get_ancestor(f, GTK_TYPE_POPOVER);
}

static gboolean check_focus_lost(gpointer _)
{
    if (np_popup_visible() && !gtk_window_is_active(np->win) && np->modal == 0 && !np_page_auth_active() &&
        !focus_in_popover())
        np_popup_hide();
    return G_SOURCE_REMOVE;
}

static void on_active_changed(GObject *w, GParamSpec *p, gpointer _)
{
    if (gtk_window_is_active(np->win)) {
        np->was_active = TRUE;
    } else if (np->config->hide_on_focus_loss && np->was_active &&
               g_get_monotonic_time() - np->shown_at > 300 * G_TIME_SPAN_MILLISECOND) {
        g_timeout_add(150, check_focus_lost, NULL);
    }
}

static gboolean on_key(GtkEventControllerKey *c, guint keyval, guint code, GdkModifierType mods, gpointer _)
{
    if (keyval == GDK_KEY_Escape) {
        np_nav_back();
        return TRUE;
    }
    return FALSE;
}

static gboolean on_close_request(GtkWindow *w, gpointer _)
{
    np_popup_hide();
    return TRUE;
}

static void setup_layer_shell(void)
{
#if HAVE_LAYER_SHELL
    if (!gtk_layer_is_supported()) return;
    NpConfig *c = np->config;
    np->layer = TRUE;
    gtk_layer_init_for_window(np->win);
    gtk_layer_set_namespace(np->win, "netpop");
    gtk_layer_set_layer(np->win, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(np->win, GTK_LAYER_SHELL_EDGE_TOP, c->anchor_top);
    gtk_layer_set_anchor(np->win, GTK_LAYER_SHELL_EDGE_BOTTOM, c->anchor_bottom);
    gtk_layer_set_anchor(np->win, GTK_LAYER_SHELL_EDGE_LEFT, c->anchor_left);
    gtk_layer_set_anchor(np->win, GTK_LAYER_SHELL_EDGE_RIGHT, c->anchor_right);
    gtk_layer_set_margin(np->win, GTK_LAYER_SHELL_EDGE_TOP, c->margin_top);
    gtk_layer_set_margin(np->win, GTK_LAYER_SHELL_EDGE_BOTTOM, c->margin_bottom);
    gtk_layer_set_margin(np->win, GTK_LAYER_SHELL_EDGE_LEFT, c->margin_left);
    gtk_layer_set_margin(np->win, GTK_LAYER_SHELL_EDGE_RIGHT, c->margin_right);
    gtk_layer_set_keyboard_mode(np->win, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
#endif
}

static void on_css_error(GtkCssProvider *p, GtkCssSection *section, GError *error, gpointer _)
{
    char *loc = gtk_css_section_to_string(section);
    g_warning("CSS %s: %s", loc, error->message);
    g_free(loc);
}

static void build_window(void)
{
    np->css = gtk_css_provider_new();
    g_signal_connect(np->css, "parsing-error", G_CALLBACK(on_css_error), NULL);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(np->css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);

    np->win = GTK_WINDOW(gtk_application_window_new(np->app));
    gtk_window_set_title(np->win, "Netpop");
    gtk_window_set_decorated(np->win, FALSE);
    gtk_window_set_resizable(np->win, FALSE);
    gtk_widget_add_css_class(GTK_WIDGET(np->win), "netpop");
    setup_layer_shell();
    /* 普通窗口模式下由合成器画边框和圆角，面板自己不再画，避免双重边框 */
    gtk_widget_add_css_class(GTK_WIDGET(np->win), np->layer ? "layer" : "toplevel");

    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(panel, "panel");
    gtk_widget_set_size_request(panel, np->config->width, -1);

    np->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_vhomogeneous(np->stack, FALSE);
    gtk_stack_set_transition_type(np->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(np->stack, 90);
    gtk_stack_add_named(np->stack, np_page_wifi_new(), NP_PAGE_WIFI);
    gtk_stack_add_named(np->stack, np_page_auth_new(), NP_PAGE_AUTH);
    gtk_stack_add_named(np->stack, np_page_details_new(), NP_PAGE_DETAILS);
    gtk_stack_add_named(np->stack, np_page_editor_new(), NP_PAGE_EDITOR);
    gtk_stack_add_named(np->stack, np_page_connections_new(), NP_PAGE_CONNECTIONS);
    gtk_stack_add_named(np->stack, np_page_new_new(), NP_PAGE_NEW);
    gtk_box_append(GTK_BOX(panel), GTK_WIDGET(np->stack));

    GtkWidget *ov = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ov), panel);
    np->toast_rev = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(np->toast_rev, GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_valign(GTK_WIDGET(np->toast_rev), GTK_ALIGN_END);
    gtk_widget_set_halign(GTK_WIDGET(np->toast_rev), GTK_ALIGN_CENTER);
    gtk_widget_set_can_target(GTK_WIDGET(np->toast_rev), FALSE);
    np->toast = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(np->toast), "toast");
    gtk_label_set_wrap(np->toast, TRUE);
    gtk_revealer_set_child(np->toast_rev, GTK_WIDGET(np->toast));
    gtk_overlay_add_overlay(GTK_OVERLAY(ov), GTK_WIDGET(np->toast_rev));
    gtk_window_set_child(np->win, ov);

    GtkEventController *kc = gtk_event_controller_key_new();
    g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(GTK_WIDGET(np->win), kc);
    g_signal_connect(np->win, "close-request", G_CALLBACK(on_close_request), NULL);
    g_signal_connect(np->win, "notify::is-active", G_CALLBACK(on_active_changed), NULL);
    load_css();
}

/* ───────────── NetworkManager ───────────── */

static void on_last_scan(void)
{
    np_page_wifi_scan_done();
    np_queue_refresh();
}

static void hook_device(NMDevice *d)
{
    g_signal_connect(d, "notify::state", G_CALLBACK(np_queue_refresh), NULL);
    if (NM_IS_DEVICE_WIFI(d)) {
        g_signal_connect(d, "access-point-added", G_CALLBACK(np_queue_refresh), NULL);
        g_signal_connect(d, "access-point-removed", G_CALLBACK(np_queue_refresh), NULL);
        g_signal_connect(d, "notify::active-access-point", G_CALLBACK(np_queue_refresh), NULL);
        g_signal_connect(d, "notify::last-scan", G_CALLBACK(on_last_scan), NULL);
    }
}

static void hook_ac(NMActiveConnection *ac)
{
    g_signal_connect(ac, "notify::state", G_CALLBACK(np_queue_refresh), NULL);
}

static void hook_conn(NMRemoteConnection *rc)
{
    g_signal_connect(rc, "changed", G_CALLBACK(np_queue_refresh), NULL);
}

static void on_device_added(NMClient *c, NMDevice *d, gpointer _) { hook_device(d); np_queue_refresh(); }
static void on_ac_added(NMClient *c, NMActiveConnection *ac, gpointer _) { hook_ac(ac); np_queue_refresh(); }
static void on_conn_added(NMClient *c, NMRemoteConnection *rc, gpointer _) { hook_conn(rc); np_queue_refresh(); }

static void client_ready(GObject *src, GAsyncResult *res, gpointer _)
{
    GError *e = NULL;
    NMClient *client = nm_client_new_finish(res, &e);
    if (!client) {
        g_warning("无法连接 NetworkManager: %s", e->message);
        np_notify("Netpop", "无法连接 NetworkManager：%s", e->message);
        g_error_free(e);
        return;
    }
    np->client = client;

    static const char *const sigs[] = {
        "notify::wireless-enabled", "notify::wireless-hardware-enabled", "notify::networking-enabled",
        "notify::nm-running", "connection-removed", "active-connection-removed", "device-removed",
    };
    for (guint i = 0; i < G_N_ELEMENTS(sigs); i++) g_signal_connect(client, sigs[i], G_CALLBACK(np_queue_refresh), NULL);
    g_signal_connect(client, "device-added", G_CALLBACK(on_device_added), NULL);
    g_signal_connect(client, "active-connection-added", G_CALLBACK(on_ac_added), NULL);
    g_signal_connect(client, "connection-added", G_CALLBACK(on_conn_added), NULL);

    const GPtrArray *a = nm_client_get_devices(client);
    for (guint i = 0; i < a->len; i++) hook_device(a->pdata[i]);
    a = nm_client_get_active_connections(client);
    for (guint i = 0; i < a->len; i++) hook_ac(a->pdata[i]);
    a = nm_client_get_connections(client);
    for (guint i = 0; i < a->len; i++) hook_conn(a->pdata[i]);

    if (np->config->agent) np_agent_start();

    np->dirty = TRUE;
    if (np_popup_visible()) {
        refresh_all();
        np_request_scan(15000);
    }
}

/* ───────────── 生命周期 ───────────── */

void np_app_startup(GtkApplication *app)
{
    np->app = app;
    np->nav = g_ptr_array_new_with_free_func(g_free);
    np->config = np_config_new();
    char *path = np_config_default_path();
    GError *e = NULL;
    if (!np_config_load_file(np->config, path, &e)) {
        g_warning("%s", e->message);
        g_error_free(e);
    }
    g_free(path);

    build_window();
    nm_client_new_async(NULL, client_ready, NULL);
}

void np_app_shutdown(void)
{
    np_page_auth_cancel();
    np_agent_stop();
    g_clear_object(&np->client);
    g_clear_pointer(&np->nav, g_ptr_array_unref);
    g_clear_pointer(&np->config, np_config_free);
    g_clear_pointer(&np->css_cache, g_free);
}
