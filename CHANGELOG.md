# Changelog

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循语义化版本。

## [0.2.0] — 2026-11-19

### 新增
- 连接编辑器，覆盖 nm-connection-editor 的常用功能：Wi-Fi（含 802.1X 企业认证、WEP、OWE、热点）、有线、WireGuard、通用 VPN、IPv4/IPv6、代理。
- “网络设置”页：按类型列出全部连接，提供“启用网络”和 Wi-Fi 总开关。
- “新建连接”页：Wi-Fi / 隐藏网络、个人热点、有线、WireGuard 模板；可导入 WireGuard 和 VPN 配置。
- Secret Agent 支持 802.1X 和 VPN 密钥；接入 Secret Service 钥匙环，兼容 nm-applet 保存的条目。
- 配置文件 `~/.config/netpop/netpop.conf`（颜色、样式、弹出位置、agent 开关）。
- 命令行参数 `--settings`、`--new`、`--edit ID`、`--version`。
- 与界面无关的 core 库，以及对应的单元测试。

### 变更
- 构建系统由 Makefile 改为 meson。
- 代码从单个文件拆分为 core 和按页面划分的 ui 模块。

### 修复
- 以普通窗口运行时出现双重边框：此模式下改由合成器绘制边框。
- 以普通窗口运行时背景透明，背后的文字透出来。
- 常驻实例收不到远程调用的命令行参数，导致 `--show`、`--hide`、`--quit` 都被当作切换显示。

## [0.1.0] — 2026-11-19

- 原型：单文件实现的 Wi-Fi 弹出面板。
