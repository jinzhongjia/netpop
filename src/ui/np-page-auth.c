/* 密码页：根据 NpSecretRequest 动态生成输入框 */
#include "np-app.h"
#include "np-pages.h"
#include "np-widgets.h"

static struct {
    NpSecretRequest *req;
    NpAuthDone done;
    gpointer data;
    GtkLabel *title, *msg;
    GtkWidget *fields;
    GPtrArray *entries; /* 与 req->fields 一一对应 */
} A;

gboolean np_page_auth_active(void)
{
    return A.req != NULL;
}

void np_page_auth_cancel(void)
{
    if (!A.req) return;
    NpSecretRequest *req = A.req;
    NpAuthDone done = A.done;
    gpointer data = A.data;
    A.req = NULL;
    done(req, FALSE, data);
}

void np_page_auth_error(const char *message)
{
    gtk_label_set_text(A.msg, message);
    gtk_widget_add_css_class(GTK_WIDGET(A.msg), "error");
}

static void submit(void)
{
    if (!A.req) return;
    for (guint i = 0; i < A.entries->len; i++) {
        char *v = g_strdup(gtk_editable_get_text(GTK_EDITABLE(A.entries->pdata[i])));
        NpSecretField *f = A.req->fields->pdata[i];
        g_free(f->value);
        f->value = v;
    }
    if (!A.done(A.req, TRUE, A.data)) return; /* 校验失败，保持页面 */
    A.req = NULL;
    np_nav_back();
}

void np_page_auth_ask(NpSecretRequest *req, NpAuthDone done, gpointer data)
{
    np_page_auth_cancel(); /* 同一时间只处理一个请求 */
    A.req = req;
    A.done = done;
    A.data = data;

    char *t = g_strdup_printf("连接到 “%s”", req->title);
    gtk_label_set_text(A.title, t);
    g_free(t);
    gtk_label_set_text(A.msg, req->message ? req->message : "");
    gtk_widget_remove_css_class(GTK_WIDGET(A.msg), "error");
    if (req->retry) gtk_widget_add_css_class(GTK_WIDGET(A.msg), "error");

    GtkWidget *c;
    while ((c = gtk_widget_get_first_child(A.fields))) gtk_box_remove(GTK_BOX(A.fields), c);
    g_ptr_array_set_size(A.entries, 0);
    GtkWidget *first_empty = NULL;
    for (guint i = 0; i < req->fields->len; i++) {
        NpSecretField *f = req->fields->pdata[i];
        GtkEntry *e = f->secret ? np_form_secret(A.fields, req->fields->len > 1 ? f->label : NULL, f->value, f->label)
                                : np_form_entry(A.fields, f->label, f->value, NULL);
        g_signal_connect_swapped(e, "activate", G_CALLBACK(submit), NULL);
        g_ptr_array_add(A.entries, e);
        if (!first_empty && (!f->value || !*f->value)) first_empty = GTK_WIDGET(e);
    }

    if (!np_popup_visible()) np_popup_show();
    np_nav_push(NP_PAGE_AUTH);
    if (first_empty || A.entries->len) gtk_widget_grab_focus(first_empty ? first_empty : A.entries->pdata[0]);
}

GtkWidget *np_page_auth_new(void)
{
    A.entries = g_ptr_array_new();
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(page), np_header("", &A.title, TRUE));
    A.msg = GTK_LABEL(np_label("", "hint", 0));
    gtk_label_set_wrap(A.msg, TRUE);
    gtk_widget_set_margin_start(GTK_WIDGET(A.msg), 4);
    gtk_widget_set_margin_end(GTK_WIDGET(A.msg), 8);
    gtk_box_append(GTK_BOX(page), GTK_WIDGET(A.msg));

    A.fields = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_end(A.fields, 8);
    gtk_box_append(GTK_BOX(page), A.fields);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btns, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btns, 4);
    gtk_widget_set_margin_end(btns, 8);
    gtk_box_append(GTK_BOX(btns), np_button("取消", NULL, G_CALLBACK(np_nav_back), NULL));
    gtk_box_append(GTK_BOX(btns), np_button("连接", "suggested", G_CALLBACK(submit), NULL));
    gtk_box_append(GTK_BOX(page), btns);
    return page;
}
