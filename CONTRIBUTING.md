# 参与贡献

欢迎提交 issue 和 pull request。

## 开发环境

```sh
# Arch Linux
sudo pacman -S meson ninja gtk4 networkmanager libsecret gtk4-layer-shell
# Debian / Ubuntu 24.04+
sudo apt install meson ninja-build libgtk-4-dev libnm-dev libsecret-1-dev libgtk4-layer-shell-dev

meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

试运行开发版本时，先退出已安装的常驻进程：

```sh
netpop --quit
./build/src/netpop --show
```

## 代码组织

```
src/
├── core/                 与界面无关，可脱离显示器和 NetworkManager 守护进程单元测试
│   ├── np-conn-model     编辑器数据模型 ⇄ NMConnection（全部校验都在这里）
│   ├── np-secrets        Secret Agent 的决策逻辑：要问哪些字段、如何应答
│   ├── np-keyring        Secret Service 存取（nm-applet 兼容格式）
│   ├── np-ap             AP 合并与排序
│   ├── np-wg             X25519 / WireGuard 密钥、配置导入
│   ├── np-config         配置文件
│   └── np-util           SSID、安全类型、地址 / 路由 / 键值解析
├── ui/                   GTK4 界面
│   ├── np-app            窗口、导航栈、弹出 / 收起、刷新调度、通知
│   ├── np-actions        对 NM 的异步操作与结果跟踪
│   ├── np-agent          Secret Agent
│   ├── np-page-*         各页面，每个页面一个文件
│   ├── np-widgets        统一外观的小部件和表单构造器
│   └── style.css         样式（只使用调色板中的命名颜色）
└── main.c                命令行入口
tests/                    GLib 测试，不能依赖正在运行的 NetworkManager 或显示器
```

设计约定：

- 新增设置项或校验逻辑时，**先在 `src/core/` 中实现并补单元测试**，再接入界面。界面只通过 `NpConnModel` 读写连接，不直接拼 NM 设置。
- 密钥类字段为空表示“保持不变”。
- NM 状态变化统一通过 `np_queue_refresh()` 合并刷新；面板隐藏时只标记为需要刷新。
- layer-shell 模式下，“点击外面收起”靠面板下方一层透明的 `netpop-backdrop` 浮层实现（layer surface 没有窗口激活状态）。不要给这个 namespace 加 blur。

## 已知问题

- 用 Tab 键切换焦点经过下拉框时，GTK 4.24 会在终端输出 `gtk_root_get_focus: assertion` 警告。这是 GTK 内部的问题，不影响功能。
- VPN 使用通用的键值编辑方式，没有接入各 VPN 插件自带的图形配置界面。

## 提交前检查

- [ ] `meson test -C build` 全部通过
- [ ] 使用 `-Dwerror=true` 编译没有警告
- [ ] 新的逻辑有对应的测试
- [ ] 界面改动在浅色和深色主题下都检查过；如有截图，请附在 PR 中
- [ ] 用户可见的变化已写入 `CHANGELOG.md` 的 “Unreleased” 一节

代码风格以 `.clang-format` 为准（4 空格缩进，每行最多 120 列）。

## 提交信息

第一行使用 `范围: 摘要` 的形式，例如 `editor: 支持 WPA3 企业认证`、`core: 修复路由解析`。
正文说明为什么要改，而不只是改了什么。

## 许可证

提交的代码按 GPL-3.0-or-later 授权；新建的源文件请在开头加上 `SPDX-License-Identifier` 注释。
