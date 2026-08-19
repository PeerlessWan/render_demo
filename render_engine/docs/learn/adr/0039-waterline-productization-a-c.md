# ADR 0039: 水位弱项产品化（A+C）

- 状态: Accepted
- 日期: 2026-08-18
- 关联: KNOWN_GAPS §3、PLAN W12–W15、POSITIONING、Sandbox

## 背景

Mega-W11 收口后，文档仍列出一批「产品水位弱项」（GI/RT、超分、VT/GPU Driven、GPU 粒子、网络传输、角色闭环、地形水植被、Linux Wayland）。目标是把它们拉到 **中台产品级 100%**，同时保留演示锚点。

## 决策

1. **边界 A**：仍是渲染中台。不做 Nanite、真 NVIDIA DDGI / RTXGI、引擎内状态复制/匹配、Frame Generation、mac/移动、C17 多窗口。
2. **边界 C**：以 Sandbox 开箱演示观感为验收锚点；Feature / Status / SKIP 必须诚实。
3. **网络产品级** = HTTP/WS/QUIC **传输**可用；复制同步归 `game_kit`（不进 `engine/net`）。
4. **GI 产品级** = ProbeVolume DDGI-lite（邻域混合 + 帧预算 + 质量档），不宣称 Lumen/RTXGI。
5. **超分链**：`IUpscaler` = DLSS（有 NGX）→ **真 FSR2**（有 FidelityFX）→ `builtin_bilinear`；禁止假名 FSR。
6. **VK bindless**：撤销 W11「钉死 SKIP」；有 `VK_EXT_descriptor_indexing` 则接 albedo 热路径，否则诚实 SKIP。
7. **里程碑**：W12 Demo 观感 → W13 规模路径 → W14 场面与传输 → W15 Linux/Wayland。

## 备选方案

- 对标 UE（改 POSITIONING 纳 Nanite/真 DDGI/复制）：否决，范围爆炸且破坏分层。
- 仅文档改口径不写代码：否决，无法满足 Sandbox 验收。

## 后果

- 优点：弱项有明确出门标准；与 game_kit / 外置项边界清晰。
- 代价：四波工程量大；第三方 SDK（FSR/NGX/MsQuic/meshoptimizer）缺库时必须 SKIP。

## 学习提示

1. 「产品级」≠「UE 级」——本 ADR 的 100% 是中台 + Sandbox。
2. 复制不在引擎；看到 net 模块只谈传输。
3. DDGI-lite 是 CPU/探针加深，不是 NVIDIA DDGI。
4. `IUpscaler::name()` 必须与真实实现一致。
5. Wayland 与 X11 并存；X11 仍是 CI 基线。
