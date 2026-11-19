# 测试数据

这些文件只供单元测试使用，**不包含任何真实凭据**：

- `ca.pem`、`client.pem`、`client.key`：用 openssl 生成的自签名测试证书和私钥，用于 802.1X TLS 相关的测试。
- `wg0.conf`：WireGuard 配置示例，使用的是 `wg(8)` 手册里公开的示例密钥。
