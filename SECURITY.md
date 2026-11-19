# 安全策略

netpop 会处理 Wi-Fi、802.1X、VPN 的密码，并作为 NetworkManager Secret Agent 运行，因此安全问题会被优先处理。

## 报告漏洞

请**不要**为安全漏洞开公开 issue。请通过 GitHub 的
[私密漏洞报告](https://github.com/jinzhongjia/netpop/security/advisories/new) 提交，
并写明受影响的版本、复现步骤和可能的影响。

一般会在 7 天内回复。修复发布后，会在发布说明中致谢报告者（除非你不希望署名）。

## 支持的版本

只有最新发布的版本会收到安全修复。

## 设计说明

- 密钥只在内存中短暂停留，按需转交给 NetworkManager 或 Secret Service，不会写入日志。
- 属于 agent 的密钥只存放在 Secret Service（例如 gnome-keyring）中，netpop 自己不落盘保存任何密钥。
- netpop 不需要 root 权限，所有特权操作都由 NetworkManager 按 polkit 规则授权。
