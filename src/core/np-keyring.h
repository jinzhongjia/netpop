// SPDX-License-Identifier: GPL-3.0-or-later
/* np-keyring.h — 通过 Secret Service（gnome-keyring 等）保存 agent 拥有的密钥
 *
 * 条目格式与 nm-applet 完全一致，因此可以直接读取 nm-applet 以前保存的密码：
 *   schema  org.freedesktop.NetworkManager.Connection
 *   属性    connection-uuid / setting-name / setting-key
 */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define NP_KEYRING_SCHEMA_NAME "org.freedesktop.NetworkManager.Connection"

gboolean np_keyring_available(void);

/* 结果为 key → value 的 GHashTable（可能为空表） */
void        np_keyring_lookup_async(const char *uuid, const char *setting_name, GCancellable *cancellable,
                                    GAsyncReadyCallback callback, gpointer user_data);
GHashTable *np_keyring_lookup_finish(GAsyncResult *result, GError **error);

void np_keyring_store(const char *uuid, const char *connection_id, const char *setting_name,
                      const char *key, const char *value);
void np_keyring_clear(const char *uuid);

G_END_DECLS
