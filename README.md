# netpop

基于 GTK4 和 libnm 的 NetworkManager 图形前端，用来替代 nm-applet 和 nm-connection-editor。
平时以弹出面板的形式挂在状态栏上，同时提供完整的连接编辑功能，并作为 Secret Agent 处理密码请求。

- 所有操作都直接通过 libnm（D-Bus）完成，不调用 nmcli，也不拼接 shell 命令
- 进程常驻，弹出或收起只是显示 / 隐藏窗口；隐藏期间不刷新界面
- 配色可以跟随外部主题文件（例如 waybar 的 `@define-color` 文件）

## 功能

| 模块 | 内容 |
| --- | --- |
| Wi-Fi | 开关、扫描、同名 AP 合并、按状态和信号排序；开放 / WPA / WPA3 网络一键连接；新网络连接失败时自动删除配置 |
| 连接详情 | 信号、频段 / 信道、速率、BSSID、IPv4 / IPv6、网关、DNS、MAC；连接、断开、删除（需二次确认） |
| 连接编辑器 | 常规（名称、绑定设备、自动连接及优先级、按流量计费、权限）；Wi-Fi（SSID、模式、频段、隐藏、MAC、MTU）；安全（WPA/WPA2、WPA3、802.1X PEAP/TTLS/TLS/PWD、WEP、OWE）；有线；WireGuard（生成密钥、多个 peer）；VPN（插件参数和密钥的通用编辑）；IPv4 / IPv6（多个地址、网关、DNS、搜索域、静态路由、隐私扩展）；代理 (PAC) |
| 新建 | Wi-Fi / 隐藏网络、个人热点、有线、WireGuard；导入 WireGuard `.conf`；通过已安装的 VPN 插件导入（如 `.ovpn`） |
| 网络设置 | 按类型列出全部连接；“启用网络”（类似飞行模式）和 Wi-Fi 总开关 |
| Secret Agent | 处理 Wi-Fi、802.1X、VPN 的密码请求；“仅当前用户”的密钥存放在 Secret Service 中，条目格式与 nm-applet 相同，原来保存的密码可以继续使用 |

修改正在使用的连接后，会先尝试热应用（reapply），NM 不支持热应用时再重新连接。

## 构建与安装

依赖：`gtk4 >= 4.12`、`libnm >= 1.40`、`glib >= 2.76`、`meson`、`ninja`。
可选依赖：`gtk4-layer-shell`（以 layer-shell 弹层显示）、`libsecret`（钥匙环）。

```sh
meson setup build --prefix ~/.local
meson compile -C build
meson test -C build        # 单元测试
meson install -C build
```

Arch Linux：`pacman -S meson gtk4 networkmanager libsecret gtk4-layer-shell`

## 使用

```
netpop                 切换显示 / 隐藏（首次运行会常驻）
netpop --show | --hide
netpop --settings      打开“网络设置”
netpop --new           打开“新建连接”
netpop --edit ID       编辑指定连接（UUID 或名称）
netpop --daemon        只在后台启动（用于登录时自启动）
netpop --quit          退出常驻进程
```

键盘操作：Esc 返回上一页，在首页按 Esc 收起；用方向键和回车在列表中选择。

### waybar

```jsonc
"network": {
  "on-click": "netpop",
  "on-click-right": "netpop --settings"
}
```

### 自启动

`~/.config/autostart/netpop.desktop` 中写 `Exec=netpop --daemon`。
netpop 运行时会注册为 Secret Agent，因此应关闭 nm-applet，避免两个 agent 都弹出密码框。

### Hyprland

使用 layer-shell 时，可以给 `netpop` 这个 namespace 加 blur 的 layerrule。
以普通窗口运行时，窗口边框由合成器绘制，面板本身不再画边框。

## 配置

`~/.config/netpop/netpop.conf`，完整示例见 [`data/netpop.conf.example`](data/netpop.conf.example)：

```ini
[appearance]
colors = ~/.config/theme/current/waybar.css   ; 定义 @bg @surface @fg @subtle @muted @blue @red @green
style  = ~/.config/netpop/style.css           ; 额外 CSS

[popup]
anchor = top-right
margin-top = 6
margin-right = 8
hide-on-focus-loss = true

[agent]
enabled = true
keyring = true
```

颜色文件和样式文件在每次弹出时重新读取，切换主题后无需重启。

## 架构

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
├── ui/                   GTK4 界面：每个页面一个文件
│   ├── np-app            窗口、导航栈、弹出 / 收起、刷新调度、通知
│   ├── np-actions        对 NM 的异步操作与结果跟踪
│   ├── np-agent          Secret Agent
│   ├── np-page-*         各页面
│   ├── np-widgets        统一外观的小部件和表单构造器
│   └── style.css         样式（只使用调色板中的命名颜色）
└── main.c                命令行入口
tests/                    GLib 测试（meson test）
```

设计约定：

- 界面层只通过 `NpConnModel` 读写连接；新增设置项时先在 core 中实现并补测试，再接入表单。
- 密钥类字段为空表示“保持不变”。
- NM 状态变化统一通过 `np_queue_refresh()` 合并刷新；面板隐藏时只标记为需要刷新。

## 开发

```sh
meson setup build && meson test -C build --print-errorlogs
```

提交前请确保 `meson test` 全部通过。代码风格见 `.clang-format` / `.editorconfig`。

## 已知问题

- 用 Tab 键切换焦点经过下拉框时，GTK 4.24 会在终端输出 `gtk_root_get_focus: assertion` 警告。这是 GTK 内部的问题，不影响功能。
- VPN 使用通用的键值编辑方式，不加载各 VPN 插件自带的图形配置界面。
