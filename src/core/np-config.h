// SPDX-License-Identifier: GPL-3.0-or-later
/* np-config.h — 用户配置（$XDG_CONFIG_HOME/netpop/netpop.conf，GKeyFile 格式）
 *
 * [appearance]
 * colors = ~/.config/theme/current/waybar.css   ; 定义 @bg @surface @fg @subtle @muted @blue @red @green
 * style  = ~/.config/netpop/style.css           ; 额外 CSS
 * width  = 368
 *
 * [popup]
 * anchor = top-right        ; top-left | top | top-right | bottom-left | bottom | bottom-right | center
 * margin-top = 6
 * margin-right = 8
 * margin-bottom = 0
 * margin-left = 0
 * hide-on-focus-loss = true
 *
 * [agent]
 * enabled = true            ; 作为 Secret Agent 处理密码请求
 * keyring = true            ; 使用 Secret Service（与 nm-applet 相同的条目格式）保存“仅当前用户”的密钥
 */
#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    char    *colors_css;
    char    *style_css;
    int      width;
    gboolean anchor_top, anchor_bottom, anchor_left, anchor_right;
    int      margin_top, margin_right, margin_bottom, margin_left;
    gboolean hide_on_focus_loss;
    gboolean agent;
    gboolean keyring;
} NpConfig;

NpConfig *np_config_new(void); /* 默认值 */
void      np_config_free(NpConfig *cfg);
char     *np_config_default_path(void);

gboolean  np_config_set_anchor(NpConfig *cfg, const char *anchor, GError **error);
gboolean  np_config_load_data(NpConfig *cfg, const char *data, gsize len, GError **error);
/* 文件不存在视为成功（保留默认值） */
gboolean  np_config_load_file(NpConfig *cfg, const char *path, GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NpConfig, np_config_free)

G_END_DECLS
