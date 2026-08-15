# ADR 0031: M19 QUIC SKIP — MsQuic 本波不捆绑

- 状态: Accepted
- 日期: 2026-08-15
- 关联: PLAN M19、ADR 0021、`engine/net`、看板 `T-https-openssl-on`

## 背景

M19 验收需要 HTTP / WebSocket / QUIC。MsQuic 体积与平台构建成本高，且本波未授权捆绑。静默安装 OpenSSL / MsQuic 违反工程约束（不静默装系统 SDK）。

## 决策

1. **本波 QUIC = SKIP**：`IQuicEndpoint` 保持 stub；`supported()==false`；`Connect`/`SendReliable` 返回 `Unavailable`，消息点名 **ADR 0031** / MsQuic 未捆绑。  
2. **不捆绑、不安装 MsQuic**；后续若启用须另批 + CMake/vendor，并可换 ngtcp2（须新 ADR，延续 0021）。  
3. **HTTPS**：继续依赖系统 OpenSSL（`ENGINE_WITH_OPENSSL` + `OPENSSL_ROOT_DIR`）；未链接时返回清晰 `Unavailable` 提示，**引擎不安装 OpenSSL**。  
4. **WebSocket**：保持 **loopback://** 回显路径可测；远程 IXWebSocket 仍可后置。  
5. 本 ADR **不修改** ADR 0021 的长期目标（仍以 MsQuic 为默认三方），仅冻结本波交付口径。

## 备选方案

- 本波强制 vendor MsQuic —— 阻塞加深波，超出授权。  
- 假实现 QUIC 回显 —— 违反「失败可诊断、禁止假成功」。

## 后果

- 优点：M19 在 Win 口径下可勾「可用加深」：HTTP 明文 + loopback WS + HTTPS/QUIC 外置可诊断。  
- 代价：无真实 QUIC 可靠流集成测，直至另批依赖。

## 学习提示

1. SKIP 也是验收结论，只要 Status 诚实。  
2. loopback WS ≠ 生产 wss；HTTPS 与 QUIC 的 TLS 依赖是两件事。  
3. 看板 `T-https-openssl-on` 仍是授权装 SDK 后的 HTTPS loopback 闸门。
