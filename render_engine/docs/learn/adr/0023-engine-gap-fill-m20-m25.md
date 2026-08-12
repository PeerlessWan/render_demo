# ADR 0023: 引擎缺口补齐纳入 M20–M25（不含 mac/移动）

- 状态: Accepted
- 日期: 2026-08-12
- 关联: PLAN §1.7 / §4.7, KNOWN_GAPS.md；修订：取消平台扩展

## 背景

相对主流渲染中台，本项目在混合 2D/3D、2D 深度、动态 GI、场景专题、GPU Driven、光追 API 对等上存在短板。曾考虑 macOS/移动，现明确 **不做**。

## 决策

1. 引擎向缺口纳入 **M20–M25**（见 PLAN）。  
2. **图形后端仅 D3D12（Windows）与 Vulkan（Windows+Linux）**；**明确不做** macOS、任何移动端、Metal。  
3. **引擎内**仍不做：玩法、脚本 VM、完整可视化编辑器、状态同步、音频 DSP、NavMesh、材质节点图编辑器（外挂 `game_kit`/`editor` 见 ADR 0027，不在本 ADR 范围）。  
4. G15 以地形+水体+植被基础可用为目标。  
5. G16 以 Vulkan RT 与 D3D12 示范路径对齐为主。  
6. **M25 之后**的中台加深项（Deferred/集群光、动画图、VT、矢量等）登记在 [KNOWN_GAPS.md](../../KNOWN_GAPS.md) §4，**不自动进入本 ADR 范围**；立项须新 ADR + PLAN 里程碑。

## 后果

- 优点：桌面双 API 路径清晰；范围可控。  
- 代价：无 Apple/移动市场；须对外说清平台边界。

## 学习提示

1. 先 M1–M19，再 M20–M25。  
2. 「仅 D3D12+Vulkan」是产品边界，不是临时延期。  
