# ADR 0044: Mega-W21 Godot 渲染内核对标 + 解冻超分/MsQuic

- 状态: Accepted（**W21 已收口**；见 [DOING_UNDO_TODO.md](../../DOING_UNDO_TODO.md)）
- 日期: 2026-08-20
- 关联: ADR 0043（冻结句由本 ADR **解冻**）、ADR 0008、ADR 0031、KNOWN_GAPS、ENGINE_VS_MAINSTREAM

## 背景

W20 封板后，自评「渲染内核 vs Godot 4」约 60–70%。缺口集中在：GI 开箱观感、2D 灯/法线、材质·粒子·体积雾产品感、以及 ADR 0043 冻结的 DLSS/FSR2/MsQuic 真 SDK。

## 决策

1. **解冻**：撤销 ADR 0043「DLSS / FSR2 / MsQuic 真 SDK 暂不开发」。  
   - Feature 名：`dlss` / `fsr2` / `quic`。  
   - CMake 可选：`ENGINE_WITH_NGX`、`ENGINE_WITH_FIDELITYFX`（或 `ENGINE_WITH_FFX` 别名）、既有 `ENGINE_WITH_MSQUIC`。  
   - **无 SDK / 无 DLL → 诚实 SKIP / `builtin_bilinear` / Probe Unavailable**；禁止假名。  
   - **不**强制捆绑进默认 CI。
2. **选型链**：`CreateUpscaler()` = DLSS（Feature+SDK）→ FSR2 → `builtin_bilinear`。  
3. **MsQuic**：`ENGINE_WITH_MSQUIC=ON` 且头文件/库可用时，`TryQuicLoopbackReliableSendRecv` 走真 API 可靠流；否则 SKIP。  
4. **Godot 对标水位（本波）**：抬「渲染内核 vs Godot」至约 **80–85%**（非整引擎）。本波交付：CascadeGi/SDFGI-lite、Light2D+法线、PbrMaterial 标准字段子集、粒子碰撞/子发射、体积雾与 Weather 打通。  
5. **仍不做**：Nanite、真 NVIDIA DDGI、Lumen、Frame Generation、mac/Metal、引擎内复制/蓝图、把玩法做进 `engine/`。

## 波次能力表（L0/L1）

| 能力 | 层级 | D3D12 | Vulkan | 备注 |
|---|---|---|---|---|
| FSR2 / DLSS 可选 SDK | L1 | 有 SDK→Ok | 有 SDK→Ok | 无→builtin；`name()` 诚实 |
| MsQuic 可靠流 loopback | L1 | — | — | `ENGINE_WITH_MSQUIC`；无→SKIP |
| CascadeGi / SDFGI-lite | L0 | 同波 | 同波 | 非 RTXGI / 非 Lumen |
| Light2D + sprite 法线/modulate | L0 | 同波 | 同波 | 无灯零差 |
| PbrMaterial 标准字段子集 | L0 | 同波 | 同波 | emission/cull/透明模式 |
| 粒子碰撞 / 子发射 | L0 | 同波 | 同波 | 缺 CS→CPU |
| Weather↔VolumetricFog | L0 | 同波 | 同波 | Medium 默认不炸 |

## 后果

- 优点：可接厂商超分与 QUIC；Godot 风格开箱感提升。  
- 代价：许可/动态加载合规；缺 SDK 时文档与 Feature 必须诚实。

## 对 ADR 0043 的修订

ADR 0043 §决策.3「冻结 DLSS/FSR2/MsQuic」**已被本 ADR 取代**。W20 其余封板内容（GI atlas、软影、VT、device 拆分等）仍有效。

## 收口备注（2026-08-20）

- 解冻：CMake `ENGINE_WITH_NGX` / `ENGINE_WITH_FIDELITYFX`（`ENGINE_WITH_FFX`）；无 SDK → `TryCreate*` nullptr → `builtin_bilinear`。
- MsQuic：`ENGINE_WITH_MSQUIC=ON` + DLL → `MsQuicOpenVersion` 真 API 路径；默认 OFF → SKIP。
- CascadeGi / Light2D / PbrMaterial 子集 / 粒子碰撞·子发射 / Weather→VolumetricFog 已落地。
- 单测：以看板跑测水位为准（W21 追加后 202+）。
- **仍不做**：Nanite / 真 DDGI / Lumen / FG / mac。
