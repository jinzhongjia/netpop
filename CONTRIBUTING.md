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

- `src/core/`：不依赖 GTK。新增设置项或校验逻辑时，**先在这里实现并补单元测试**，再接入界面。
- `src/ui/`：每个页面一个 `np-page-*.c`。界面只通过 `NpConnModel` 读写连接，不直接拼 NM 设置。
- `tests/`：GLib 测试，`meson test` 统一运行。测试不能依赖正在运行的 NetworkManager 或显示器。

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
