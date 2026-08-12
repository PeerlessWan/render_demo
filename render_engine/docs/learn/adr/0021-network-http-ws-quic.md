# ADR 0021: 网络层 — HTTP / WebSocket / QUIC 可靠流；轻量三方封装

- 状态: Accepted
- 日期: 2026-08-12
- 关联: PLAN §1.5 / M19, engine/net

## 背景

引擎需要跨平台拉取资源、遥测上报、轻量信令/RPC。自研完整协议栈成本高；重型中间件会绑架架构。需要 **HTTP、WebSocket、可靠 QUIC**，且保持与「无玩法同步」边界。

## 决策

1. 提供 `engine/net` **抽象**：`IHttpClient`、`IWebSocket`、`IQuicEndpoint`；业务不直链三方；换库只改适配层。  
2. 默认三方实现（轻量、跨 Windows/Linux）：  
   - **HTTP(S)**：[cpp-httplib](https://github.com/yhirose/cpp-httplib)（header-only；可换 libcurl 若需代理/奇葩环境）  
   - **WebSocket**：[IXWebSocket](https://github.com/machinezone/IXWebSocket)  
   - **QUIC 可靠流**：[MsQuic](https://github.com/microsoft/msquic)（IETF QUIC；可靠 stream 为主路径）  
3. 主循环 `Net.Pump()` 派发回调；IO 不阻塞渲染线程。  
4. **不做**：游戏状态同步、匹配大厅、反作弊、自研加密协议。  
5. 里程碑：**M19**；可与渲染里程碑并行。

## 备选方案

- 仅 libcurl + 自研 WS —— HTTP 强，WS/QUIC 仍要补。  
- Boost.Beast / ASIO 全家桶 —— 偏重，教学与编译成本高。  
- ngtcp2 + nghttp3 替代 MsQuic —— 允许，须另开 ADR 并保证 Win/Linux 对等。

## 后果

- 优点：传输能力清晰、依赖可控、与玩法解耦。  
- 代价：三套库的 TLS/构建差异；CI 需 loopback 集成测。

## 学习提示

1. QUIC「可靠」指 stream 可靠投递，不是「不可靠 UDP 游戏包」。  
2. 先搞懂 Pump/回调线程，再谈业务协议。  
3. 网络失败必须可诊断，和视频硬解同一风格。  
