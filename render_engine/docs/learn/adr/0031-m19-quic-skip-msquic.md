# ADR 0031: QUIC — MsQuic 可选启用（存在则 Feature；否则 Unavailable SKIP）

- 状态: Accepted（W8 修订）
- 日期: 2026-08-17
- 关联: PLAN M19、ADR 0021、`engine/net`、看板 `T-https-openssl-on`、Mega-W8

## 背景

M19 验收需要 HTTP / WebSocket / QUIC。MsQuic 体积与平台构建成本高，且**禁止静默安装**系统 SDK。本波允许在本机**已存在** MsQuic DLL/lib 时可选启用；未找到则诚实 SKIP。

## 决策

1. **默认不捆绑 MsQuic**；`ENGINE_WITH_MSQUIC` 仅在 `third_party/msquic` 或显式路径就绪时默认 ON，否则 OFF。  
2. **运行时探测**：`ProbeMsQuicPresent` / `ProbeAndSetQuicFeature`（`LoadLibrary("msquic.dll")` 或 `ENGINE_MSQUIC_DLL_PATH`）。存在 → `Feature quic=true`；缺失 → `quic=false`。  
3. **`IQuicEndpoint`**：`NetSystem` 挂 `QuicEndpointHook`。缺失时 `supported()==false`，`Connect`/`SendReliable` 返回 `Unavailable`，消息点名 **ADR 0031** / optional enable。  
4. **存在时仍为 link/Connect stub**：本波不要求完整可靠流会话；`TryQuicConnectStub` 在 Feature/探测为真时仍返回可诊断 `Unavailable`（禁止假成功），直至另批接真 API。  
5. **HTTPS**：继续依赖系统 OpenSSL（`ENGINE_WITH_OPENSSL`）；引擎不安装 OpenSSL / MsQuic。  
6. 长期目标仍见 ADR 0021（MsQuic 为默认三方候选）；本 ADR 只定义**可选探测启用**口径。

## 备选方案

- 强制 vendor MsQuic —— 违反「不静默安装」与本波边界。  
- 假实现 QUIC 回显 —— 违反「失败可诊断、禁止假成功」。

## 后果

- 优点：无 MsQuic 机器保持 SKIP；有 DLL/lib 时可打开 Feature 与后续接线入口。  
- 代价：完整 QUIC 可靠流集成测仍后置，直至真 Connect 接线。

## 学习提示

1. SKIP / Unavailable 也是验收结论，只要 Status 诚实。  
2. `Feature quic` ≠ 生产会话已通；先看 `QueryMsQuicProbeInfo().detail`。  
3. 看板 `T-https-openssl-on` 与 QUIC TLS 依赖是两件事。
