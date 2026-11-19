# netpop

[![CI](https://github.com/jinzhongjia/netpop/actions/workflows/ci.yml/badge.svg)](https://github.com/jinzhongjia/netpop/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

基于 GTK4 和 libnm 的 NetworkManager 网络面板，用来替代 nm-applet 和 nm-connection-editor。

- 状态栏弹出面板：Wi-Fi 列表、一键连接、连接详情
- 完整的连接编辑器：Wi-Fi（含 802.1X、WPA3、热点）、有线、WireGuard、VPN、IPv4/IPv6、代理
- 作为 Secret Agent 处理密码请求，兼容 nm-applet 保存在钥匙环里的密码
- 常驻进程，弹出几乎没有延迟；配色可以跟随外部主题文件

## 安装

依赖：gtk4 ≥ 4.12、libnm ≥ 1.40、meson。可选：gtk4-layer-shell（固定在状态栏下方的弹层）、libsecret（钥匙环）。

```sh
meson setup build --prefix ~/.local
meson install -C build
```

Arch Linux：`cd packaging/arch && makepkg -si`

## 使用

```
netpop              切换显示 / 隐藏
netpop --settings   网络设置（全部连接）
netpop --edit ID    编辑指定连接（UUID 或名称）
netpop --daemon     后台常驻（用于自启动）
netpop --quit       退出
```

waybar 示例：

```jsonc
"network": { "on-click": "netpop", "on-click-right": "netpop --settings" }
```

登录时运行 `netpop --daemon`，并关闭 nm-applet（两者都是 Secret Agent，会重复弹出密码框）。

## 配置

`~/.config/netpop/netpop.conf`，所有选项见 [netpop.conf.example](data/netpop.conf.example)：

```ini
[appearance]
colors = ~/.config/theme/current/waybar.css   ; 用 @define-color 定义调色板的 CSS 文件

[popup]
anchor = top-right
```

## 开发

见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可证

[GPL-3.0-or-later](LICENSE)
