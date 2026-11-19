// SPDX-License-Identifier: GPL-3.0-or-later
/* np-pages.h — 各页面的入口 */
#pragma once

#include <gtk/gtk.h>
#include <NetworkManager.h>

#include "np-conn-model.h"
#include "np-secrets.h"

G_BEGIN_DECLS

/* 主页：Wi-Fi 列表 */
GtkWidget *np_page_wifi_new(void);
void       np_page_wifi_refresh(void);
void       np_page_wifi_scan_done(void);

/* 连接详情 */
GtkWidget *np_page_details_new(void);
void       np_page_details_open(NMRemoteConnection *rc);
void       np_page_details_refresh(void);
void       np_page_details_leave(void);

/* 连接编辑器 */
GtkWidget *np_page_editor_new(void);
void       np_page_editor_open(NMRemoteConnection *rc);
/* 新建：model 与 base（可为 NULL，例如导入的连接）的所有权都转移给编辑器 */
void       np_page_editor_open_new(NpConnModel *model, NMConnection *base, gboolean activate_after);
void       np_page_editor_leave(void);

/* 全部连接 / 新建连接 */
GtkWidget *np_page_connections_new(void);
void       np_page_connections_refresh(void);
GtkWidget *np_page_new_new(void);

/* 密码页：done 在确认（ok=TRUE）或取消（ok=FALSE）时调用。
 * 确认时返回 FALSE 表示校验失败、页面保持打开（应先调用 np_page_auth_error），之后还会再次调用。
 * done 返回 TRUE 后页面不再引用 req，req 由 done 负责释放。 */
typedef gboolean (*NpAuthDone)(NpSecretRequest *req, gboolean ok, gpointer data);
GtkWidget *np_page_auth_new(void);
void       np_page_auth_ask(NpSecretRequest *req, NpAuthDone done, gpointer data);
void       np_page_auth_error(const char *message);
void       np_page_auth_cancel(void);
gboolean   np_page_auth_active(void);

/* Secret Agent */
void np_agent_start(void);
void np_agent_stop(void);

G_END_DECLS
