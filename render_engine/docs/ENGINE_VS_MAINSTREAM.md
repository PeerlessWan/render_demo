# 与主流引擎能力对标（自评）

> **性质：** 工程观感自评，不是跑分或功能勾选表。  
> **基准日期：** 2026-08-20（**W21** ADR 0044；HEAD 见 [DOING_UNDO_TODO.md](DOING_UNDO_TODO.md)）。  
> **本引擎定位：** 桌面 **渲染中台**（Win D3D12+Vulkan），**不是** 全能游戏引擎。  
> 相关：[POSITIONING.md](POSITIONING.md)、[KNOWN_GAPS.md](KNOWN_GAPS.md)、[PLAN.md](PLAN.md) §1.9、[ADR 0044](learn/adr/0044-w21-godot-parity-unfreeze.md)。

## 1. 两把尺子

| 尺子 | 比什么 | 不比什么 |
|---|---|---|
| **渲染内核** | 光栅主路径、光影、后处理、GI/RT 深度、GPU Driven、双后端 | 编辑器、脚本、导出、生态、玩法 |
| **完整游戏引擎** | 上者 + 编辑器 + 脚本一等公民 + 音频/多人/平台导出 + 资产生态 | — |

百分比 =「相对该参照引擎产品能力面的可感知重合」，**本口径封板 100% ≠ 对标主流 100%**。

## 2. 综合完成度

| 参照 | 当渲染内核 | 当完整游戏引擎 | 读法 |
|---|---:|---:|---|
| **Godot 4** | **约 80–85%** | **约 35–45%** | W21 抬 GI/2D/超分钩子/粒子；整引擎仍输编辑器/脚本/导出 |
| **Unity（URP/HDRP 综合）** | **约 50–60%** | **约 20–30%** | 内核略抬；整引擎仍扣 Editor/C#/生态 |
| **UE5** | **约 28–38%** | **约 10–20%** | 仍被 Lumen/Nanite/TSR 拉开 |

**一句话：**

- Godot：内核约八成（W21 目标水位）；整引擎三四成。  
- Unity：内核约一半出头；整引擎两三成。  
- UE5：内核仍约三成；整引擎一两成。

## 3. 分域粗表（vs 各参照的「渲染向」）

| 功能域 | vs Godot | vs Unity | vs UE5 | 本引擎要点 |
|---|---:|---:|---:|---|
| 基础 3D 光栅 | 70–80% | 60–70% | 50–60% | Forward+ / CSM / IBL |
| 后处理 | 65–75% | 55–65% | 40–50% | SSAO/TAA/SSR/Bloom… |
| 动态 GI | 55–70% | 30–40% | 18–28% | CascadeGi / SDFGI-lite；非 Lumen/真 DDGI |
| 光追 | 30–45% | 25–35% | 15–25% | DXR 示范 + 软影 half-res |
| GPU Driven / 几何 | 50–60% | 40–50% | 20–30% | Cull/Indirect；**无 Nanite** |
| 超分 | 40–55% | 15–25% | 10–20% | DLSS→FSR2→bilinear（无 SDK 诚实） |
| 2D | 60–70% | 45–55% | 40–50% | Light2D + 法线/modulate/层 |
| 物理 | 55–65% | 45–55% | 35–45% | Jolt；无载具/服装管线 |
| 音频 | 20–30% | 15–25% | 10–20% | 播控 only；不做 DSP |
| 编辑器/工具 | 15–25% | 10–20% | 5–15% | CLI + 外挂 editor |
| 脚本/玩法（引擎内） | ~0% | ~0% | ~0% | 外挂 `game_kit` |
| 网络/复制 | 20–30% | 12–22% | 5–15% | HTTP/WS；MsQuic 可选；无状态同步 |
| 平台覆盖 | 30–40% | 25–35% | 20–30% | Win+Linux；无 mac/移动 |
| 双后端对齐纪律 | 80–100% | 70–90% | 60–80% | VULKAN_PARITY + Feature/SKIP |

## 4. 明确不做（拉低「整引擎」分数的主因）

- Nanite / 真 NVIDIA DDGI / Lumen 级 GI  
- Frame Generation  
- macOS / 移动 / Metal  
- 引擎内脚本 VM、完整可视化编辑器、状态同步/复制  
- 音频 DSP / 商业资产生态  

详见 [POSITIONING.md](POSITIONING.md) §2–3、[DOING_UNDO_TODO.md](DOING_UNDO_TODO.md)。

## 5. 工作区补一层之后

| 层 | 对「完整游戏引擎」分数的影响 |
|---|---|
| `game_kit` GK0–GK5 已接线 | 略抬脚本/关卡流，仍远低于 Unity/Godot 一等公民 |
| `editor` ED 已收口 | 略抬摆关，仍远低于三大编辑器 |
| `genre_kits` / 新品类 | 仍弱项（暂停） |

口径：[PLAN.md](PLAN.md) **§1.9**。

## 6. 修订

W21（ADR 0044）解冻超分/MsQuic，并加深 CascadeGi / Light2D / 材质·粒子·体积雾；目标渲染内核 vs Godot **约 80–85%**。
