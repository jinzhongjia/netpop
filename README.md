# netpop

GTK4 + libnm 写的 NetworkManager Wi-Fi 弹出面板，用来替代 nm-applet / nm-connection-editor 的日常操作。

- 直接调用 libnm（D-Bus），不经过 nmcli 或 shell
- 进程常驻，弹出/收起只是显示或隐藏窗口（约 30 ms）；隐藏时不刷新界面
- 自身就是 Secret Agent：密码输错或改了密码时，由它弹出密码页
- 配色读取 `~/.config/theme/current/waybar.css`，每次弹出时重新读取，跟随主题切换；
  如需额外覆盖样式，写在 `~/.config/netpop/style.css`

## 功能

- Wi-Fi 开关、重新扫描、按信号强度排序（同名 AP 合并）
- 点击已保存的网络直接连接；开放网络一键连接；WPA/WPA2/WPA3 网络输入密码后连接
- 新网络连接失败时自动删除，不会留下密码错误的配置
- 详情页：信号、频段/信道、速率、BSSID、IPv4/IPv6、网关、DNS、MAC
- 设置：自动连接、修改/查看密码、IPv4 自动/手动、DNS；已连接的网络保存后会尽量热应用（reapply）
- 忘记网络（需点两次确认）、断开
- 有线 / VPN / WireGuard 连接：点击切换连接状态
- 企业级 (802.1X) / WEP 网络和其他高级选项：底部“高级设置”会打开 nm-connection-editor

## 构建

```sh
make && make install      # 安装到 ~/.local/bin，并加入 XDG autostart
```

如果装了 `gtk4-layer-shell`，会自动编译成 layer-shell 弹层（锚定在右上角、带模糊，不进入窗口栈）。
可用 `NETPOP_MARGIN_TOP` / `NETPOP_MARGIN_RIGHT` 调整位置。

## 用法

```
netpop            切换显示 / 隐藏
netpop --show | --hide | --daemon | --quit
```

快捷键：Esc 返回上一页 / 收起；方向键 + 回车在列表中选择。

设置 `NETPOP_NO_AGENT=1` 可以不注册 Secret Agent。
