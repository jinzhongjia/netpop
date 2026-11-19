// SPDX-License-Identifier: GPL-3.0-or-later
/* np-widgets.h — 统一外观的小部件与表单构造器 */
#pragma once

#include <gtk/gtk.h>

#include "np-conn-model.h"

G_BEGIN_DECLS

GtkWidget *np_glyph(const char *glyph, const char *css_class);
GtkWidget *np_label(const char *text, const char *css_class, float xalign);
GtkWidget *np_button(const char *text, const char *css_class, GCallback cb, gpointer data);
GtkWidget *np_icon_button(const char *glyph, const char *tooltip, GCallback cb, gpointer data);
GtkWidget *np_glyph_button(const char *glyph, const char *text, const char *css_class, GCallback cb, gpointer data);

/* 页面标题栏；back 为 TRUE 时带返回按钮（调用 np_nav_back） */
GtkWidget *np_header(const char *title, GtkLabel **title_out, gboolean back);
GtkWidget *np_scroller(GtkWidget *child, int max_height);
GtkWidget *np_spacer(void);

/* 在 parent 中追加 “小标题 + 卡片”，返回卡片（纵向盒子） */
GtkWidget *np_section(GtkWidget *parent, const char *title);

/* 统一的列表行 */
GtkWidget *np_list_row(const char *glyph, const char *title, const char *subtitle, gboolean highlight,
                       gboolean busy, gboolean locked, GtkWidget *suffix);
void       np_list_clear(GtkListBox *box);
GtkListBox *np_list_new(void);

/* ── 表单：在 box 中追加带标签的字段，返回输入部件 ── */
GtkEntry      *np_form_entry(GtkWidget *box, const char *label, const char *value, const char *placeholder);
GtkEntry      *np_form_secret(GtkWidget *box, const char *label, const char *value, const char *placeholder);
GtkSwitch     *np_form_switch(GtkWidget *box, const char *label, const char *hint, gboolean active);
GtkDropDown   *np_form_choice(GtkWidget *box, const char *label, const NpChoice *choices, guint n,
                              const char *value, const char *custom_label);
GtkTextView   *np_form_text(GtkWidget *box, const char *label, const char *value, const char *hint);
GtkSpinButton *np_form_spin(GtkWidget *box, const char *label, double min, double max, double value);
GtkEntry      *np_form_file(GtkWidget *box, const char *label, const char *value, const char *placeholder);
GtkLabel      *np_form_hint(GtkWidget *box, const char *text);

/* 取值（返回新分配、已去首尾空白的字符串） */
char       *np_entry_text(GtkEntry *e);
char       *np_text_view_text(GtkTextView *tv);
/* np_form_choice 的值：choices[i].value，或自定义项的原值 */
const char *np_choice_value(GtkDropDown *dd, const NpChoice *choices, guint n);
guint       np_choice_selected(GtkDropDown *dd);

/* 字段所在的整行（标签 + 输入），用于显示 / 隐藏 */
GtkWidget *np_field(gpointer input);
void       np_field_set_visible(gpointer input, gboolean visible);

G_END_DECLS
