// SPDX-License-Identifier: GPL-3.0-or-later
/* np-app.h — 应用全局状态：窗口、页面导航、弹出 / 收起、通知、刷新调度 */
#pragma once

#include <gtk/gtk.h>
#include <NetworkManager.h>

#include "config.h"
#include "np-config.h"

G_BEGIN_DECLS

/* Nerd Font（Material Design）图标 */
#define NP_G_W4       "\U000F0928"
#define NP_G_W3       "\U000F0925"
#define NP_G_W2       "\U000F0922"
#define NP_G_W1       "\U000F091F"
#define NP_G_W0       "\U000F092F"
#define NP_G_LOCK     "\U000F033E"
#define NP_G_CHEV     "\U000F0142"
#define NP_G_BACK     "\U000F0141"
#define NP_G_REFRESH  "\U000F0450"
#define NP_G_ETHERNET "\U000F0200"
#define NP_G_VPN      "\U000F0582"
#define NP_G_SETTINGS "\U000F0493"
#define NP_G_PLUS     "\U000F0415"
#define NP_G_HOTSPOT  "\U000F0003"
#define NP_G_IMPORT   "\U000F02FA"
#define NP_G_KEY      "\U000F0306"
#define NP_G_HIDDEN   "\U000F0209"
#define NP_G_NETWORK  "\U000F0317"
#define NP_G_WIFI     "\U000F05A9"

#define NP_PAGE_WIFI        "wifi"
#define NP_PAGE_AUTH        "auth"
#define NP_PAGE_DETAILS     "details"
#define NP_PAGE_EDITOR      "editor"
#define NP_PAGE_CONNECTIONS "connections"
#define NP_PAGE_NEW         "new"

typedef struct {
    GtkApplication *app;
    NpConfig       *config;
    NMClient       *client;

    GtkWindow      *win;
    GtkStack       *stack;
    GPtrArray      *nav; /* 返回栈：页面名 */
    GtkCssProvider *css;
    char           *css_cache;
    gboolean        layer;

    int      modal; /* >0：文件选择器等外部窗口打开中，不因失焦收起 */
    gint64   shown_at;
    gboolean was_active;
    gboolean dirty;
    guint    refresh_id;

    GtkRevealer *toast_rev;
    GtkLabel    *toast;
    guint        toast_id;
} NpApp;

extern NpApp *np;

void np_app_startup(GtkApplication *app);
void np_app_shutdown(void);

gboolean np_popup_visible(void);
void     np_popup_show(void);
void     np_popup_hide(void);
void     np_popup_toggle(void);

void        np_nav_push(const char *page);
void        np_nav_back(void);
void        np_nav_home(void);
const char *np_nav_current(void);

/* 弹出层可见时显示 toast，否则发送桌面通知（title 为 NULL 时只显示 toast） */
void np_notify(const char *title, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

/* NM 状态变化后调用：可见时合并刷新，隐藏时只标记脏 */
void np_queue_refresh(void);

void np_modal_begin(void);
void np_modal_end(void);

G_END_DECLS
