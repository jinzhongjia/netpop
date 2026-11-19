// SPDX-License-Identifier: GPL-3.0-or-later
#include "np-widgets.h"
#include "np-app.h"

GtkWidget *np_glyph(const char *g, const char *cls)
{
    GtkWidget *l = gtk_label_new(g);
    gtk_widget_add_css_class(l, "glyph");
    if (cls) gtk_widget_add_css_class(l, cls);
    return l;
}

GtkWidget *np_label(const char *text, const char *cls, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls) gtk_widget_add_css_class(l, cls);
    return l;
}

GtkWidget *np_button(const char *text, const char *cls, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new_with_label(text);
    if (cls) gtk_widget_add_css_class(b, cls);
    if (cb) g_signal_connect_swapped(b, "clicked", cb, data);
    return b;
}

GtkWidget *np_icon_button(const char *g, const char *tip, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(b), np_glyph(g, NULL));
    gtk_widget_add_css_class(b, "icon");
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    if (tip) gtk_widget_set_tooltip_text(b, tip);
    if (cb) g_signal_connect_swapped(b, "clicked", cb, data);
    return b;
}

GtkWidget *np_glyph_button(const char *g, const char *text, const char *cls, GCallback cb, gpointer data)
{
    GtkWidget *b = gtk_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), np_glyph(g, NULL));
    gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    gtk_button_set_child(GTK_BUTTON(b), box);
    if (cls) gtk_widget_add_css_class(b, cls);
    if (cb) g_signal_connect_swapped(b, "clicked", cb, data);
    return b;
}

GtkWidget *np_header(const char *title, GtkLabel **title_out, gboolean back)
{
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(h, "header");
    if (back) gtk_box_append(GTK_BOX(h), np_icon_button(NP_G_BACK, "返回 (Esc)", G_CALLBACK(np_nav_back), NULL));
    GtkWidget *t = np_label(title, "title", 0);
    gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(t, TRUE);
    gtk_widget_set_margin_start(t, back ? 2 : 6);
    gtk_box_append(GTK_BOX(h), t);
    if (title_out) *title_out = GTK_LABEL(t);
    return h;
}

GtkWidget *np_scroller(GtkWidget *child, int max_height)
{
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), max_height);
    /* 右侧 8px 留给悬浮滚动条，见 style.css 中 .panel 的内边距 */
    if (child) {
        gtk_widget_set_margin_end(child, 8);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), child);
    }
    return sc;
}

GtkWidget *np_spacer(void)
{
    GtkWidget *s = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(s, TRUE);
    return s;
}

GtkWidget *np_section(GtkWidget *parent, const char *title)
{
    if (title) gtk_box_append(GTK_BOX(parent), np_label(title, "section", 0));
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "card");
    gtk_box_append(GTK_BOX(parent), card);
    return card;
}

GtkWidget *np_list_row(const char *g, const char *title, const char *sub, gboolean highlight, gboolean busy,
                       gboolean locked, GtkWidget *suffix)
{
    GtkWidget *row = gtk_list_box_row_new();
    if (highlight) gtk_widget_add_css_class(row, "active");

    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    if (g) gtk_box_append(GTK_BOX(h), np_glyph(g, "sig"));

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
    GtkWidget *t = np_label(title, "name", 0);
    gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(v), t);
    if (sub && *sub) {
        GtkWidget *s = np_label(sub, "sub", 0);
        gtk_label_set_ellipsize(GTK_LABEL(s), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(v), s);
    }
    gtk_box_append(GTK_BOX(h), v);

    if (busy) {
        GtkWidget *sp = gtk_spinner_new();
        gtk_spinner_start(GTK_SPINNER(sp));
        gtk_box_append(GTK_BOX(h), sp);
    }
    if (locked) gtk_box_append(GTK_BOX(h), np_glyph(NP_G_LOCK, "lock"));
    if (suffix) gtk_box_append(GTK_BOX(h), suffix);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), h);
    return row;
}

GtkListBox *np_list_new(void)
{
    GtkListBox *l = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(l, GTK_SELECTION_NONE);
    return l;
}

void np_list_clear(GtkListBox *box)
{
    /* gtk_list_box_remove_all() 会连 placeholder 一起移除，这里只删行 */
    GtkWidget *c = gtk_widget_get_first_child(GTK_WIDGET(box));
    while (c) {
        GtkWidget *next = gtk_widget_get_next_sibling(c);
        if (GTK_IS_LIST_BOX_ROW(c)) gtk_list_box_remove(box, c);
        c = next;
    }
}

/* ── 表单 ── */

static GtkWidget *field_box(GtkWidget *box, const char *label, GtkWidget *input)
{
    GtkWidget *f = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(f, "field");
    if (label) gtk_box_append(GTK_BOX(f), np_label(label, "key", 0));
    gtk_box_append(GTK_BOX(f), input);
    gtk_box_append(GTK_BOX(box), f);
    return f;
}

GtkWidget *np_field(gpointer input)
{
    GtkWidget *f = g_object_get_data(G_OBJECT(input), "np-field");
    return f ? f : gtk_widget_get_parent(GTK_WIDGET(input));
}

void np_field_set_visible(gpointer input, gboolean visible)
{
    gtk_widget_set_visible(np_field(input), visible);
}

GtkEntry *np_form_entry(GtkWidget *box, const char *label, const char *value, const char *ph)
{
    GtkWidget *e = gtk_entry_new();
    if (value) gtk_editable_set_text(GTK_EDITABLE(e), value);
    if (ph) gtk_entry_set_placeholder_text(GTK_ENTRY(e), ph);
    field_box(box, label, e);
    return GTK_ENTRY(e);
}

static void on_reveal(GtkEntry *e, GtkEntryIconPosition pos, gpointer _)
{
    gboolean show = !gtk_entry_get_visibility(e);
    gtk_entry_set_visibility(e, show);
    gtk_entry_set_icon_from_icon_name(e, GTK_ENTRY_ICON_SECONDARY, show ? "view-conceal-symbolic" : "view-reveal-symbolic");
}

GtkEntry *np_form_secret(GtkWidget *box, const char *label, const char *value, const char *ph)
{
    GtkEntry *e = np_form_entry(box, label, value, ph);
    gtk_entry_set_visibility(e, FALSE);
    gtk_entry_set_input_purpose(e, GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_icon_from_icon_name(e, GTK_ENTRY_ICON_SECONDARY, "view-reveal-symbolic");
    gtk_entry_set_icon_tooltip_text(e, GTK_ENTRY_ICON_SECONDARY, "显示 / 隐藏");
    g_signal_connect(e, "icon-press", G_CALLBACK(on_reveal), NULL);
    return e;
}

GtkSwitch *np_form_switch(GtkWidget *box, const char *label, const char *hint, gboolean active)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "field");
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(v, TRUE);
    gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(v), np_label(label, NULL, 0));
    if (hint) {
        GtkWidget *h = np_label(hint, "hint", 0);
        gtk_label_set_wrap(GTK_LABEL(h), TRUE);
        gtk_box_append(GTK_BOX(v), h);
    }
    gtk_box_append(GTK_BOX(row), v);
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), active);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), sw);
    gtk_box_append(GTK_BOX(box), row);
    return GTK_SWITCH(sw);
}

GtkDropDown *np_form_choice(GtkWidget *box, const char *label, const NpChoice *choices, guint n,
                            const char *value, const char *custom_label)
{
    GtkStringList *list = gtk_string_list_new(NULL);
    for (guint i = 0; i < n; i++) gtk_string_list_append(list, choices[i].label);
    guint sel = np_choice_index(choices, n, value);
    if (sel == n) {
        /* 不在预设里的值（例如自定义 MAC）作为额外一项保留 */
        gtk_string_list_append(list, custom_label ? custom_label : value);
    }
    GtkWidget *dd = gtk_drop_down_new(G_LIST_MODEL(list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), sel);
    g_object_set_data_full(G_OBJECT(dd), "np-custom", g_strdup(sel == n ? value : NULL), g_free);
    field_box(box, label, dd);
    return GTK_DROP_DOWN(dd);
}

const char *np_choice_value(GtkDropDown *dd, const NpChoice *choices, guint n)
{
    guint sel = gtk_drop_down_get_selected(dd);
    if (sel < n) return choices[sel].value;
    return g_object_get_data(G_OBJECT(dd), "np-custom");
}

guint np_choice_selected(GtkDropDown *dd)
{
    return gtk_drop_down_get_selected(dd);
}

GtkTextView *np_form_text(GtkWidget *box, const char *label, const char *value, const char *hint)
{
    GtkWidget *tv = gtk_text_view_new();
    gtk_widget_add_css_class(tv, "textarea");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(tv), FALSE);
    gtk_widget_set_size_request(tv, -1, 56);
    if (value) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), value, -1);
    GtkWidget *f = field_box(box, label, tv);
    if (hint) {
        GtkWidget *h = np_label(hint, "hint", 0);
        gtk_label_set_wrap(GTK_LABEL(h), TRUE);
        gtk_box_append(GTK_BOX(f), h);
    }
    return GTK_TEXT_VIEW(tv);
}

GtkSpinButton *np_form_spin(GtkWidget *box, const char *label, double min, double max, double value)
{
    GtkWidget *sp = gtk_spin_button_new_with_range(min, max, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sp), value);
    field_box(box, label, sp);
    return GTK_SPIN_BUTTON(sp);
}

static void file_chosen(GObject *src, GAsyncResult *res, gpointer data)
{
    GtkEntry *e = data;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    np_modal_end();
    if (f) {
        char *path = g_file_get_path(f);
        if (path) gtk_editable_set_text(GTK_EDITABLE(e), path);
        g_free(path);
        g_object_unref(f);
    }
    g_object_unref(e);
}

static void on_browse(GtkEntry *e)
{
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_modal(d, TRUE);
    const char *cur = gtk_editable_get_text(GTK_EDITABLE(e));
    if (cur && *cur) {
        GFile *f = g_file_new_for_path(cur);
        gtk_file_dialog_set_initial_file(d, f);
        g_object_unref(f);
    }
    np_modal_begin();
    gtk_file_dialog_open(d, np->win, NULL, file_chosen, g_object_ref(e));
    g_object_unref(d);
}

GtkEntry *np_form_file(GtkWidget *box, const char *label, const char *value, const char *ph)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *e = gtk_entry_new();
    gtk_widget_set_hexpand(e, TRUE);
    if (value) gtk_editable_set_text(GTK_EDITABLE(e), value);
    if (ph) gtk_entry_set_placeholder_text(GTK_ENTRY(e), ph);
    gtk_box_append(GTK_BOX(row), e);
    gtk_box_append(GTK_BOX(row), np_button("选择…", NULL, G_CALLBACK(on_browse), e));
    GtkWidget *f = field_box(box, label, row);
    g_object_set_data(G_OBJECT(e), "np-field", f);
    return GTK_ENTRY(e);
}

GtkLabel *np_form_hint(GtkWidget *box, const char *text)
{
    GtkWidget *l = np_label(text, "hint", 0);
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_box_append(GTK_BOX(box), l);
    return GTK_LABEL(l);
}

char *np_entry_text(GtkEntry *e)
{
    return g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(e))));
}

char *np_text_view_text(GtkTextView *tv)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(tv);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    return g_strstrip(gtk_text_buffer_get_text(b, &s, &e, FALSE));
}
