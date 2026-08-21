# 与主流引擎能力对标（自评）

> **性质：** 工程观感自评，不是跑分或功能勾选表。  
> **基准日期：** 2026-08-21（W26 / [ADR 0049](learn/adr/0049-godot-2d-physics-api.md)；看板 [DOING_UNDO_TODO.md](DOING_UNDO_TODO.md)）。  
> **本引擎定位：** 桌面 **渲染中台**（Win D3D12+Vulkan），**不是** 全能游戏引擎。  
> **外挂编辑器：** 中小关卡 vs Godot ≈95% — 见 [`editor/docs/ENGINE_VS_GODOT_EDITOR.md`](../../editor/docs/ENGINE_VS_GODOT_EDITOR.md)。  
> **读法：** W25「已收口」= Feature / 缓冲合同 / SKIP 纪律；**≠** 厂商 SDK 真 Dispatch 或与 Godot/Unity 产品视觉等价。

## 1. 两把尺子

| 尺子 | 比什么 | 不比什么 |
|---|---|---|
| **渲染内核** | 光栅、光影、后处理、GI、2D 灯影、超分链、双后端 | 引擎内编辑器、脚本、导出、生态 |
| **完整游戏引擎** | 上者 + 编辑器 + 脚本 + 导出 + 资产生态 | — |

## 2. 综合完成度（2026-08-21 审计）

| 参照 | 当渲染内核 | 当完整游戏引擎 | 读法 |
|---|---:|---:|---|
| **Godot 4** | **约 60–72%** | **约 32–42%** | Forward+ / SKIP 纪律强；2D+Physics2D API 加深（ADR 0049）；RT/超分/VG 偏合同与教学桩 |
| **Unity** | **约 40–55%** | **约 15–25%** | 无完整 HDRP RT / 真 DLSS 产品链 |
| **UE5** | **约 20–35%** | **约 10–18%** | 无 Lumen/FG；Nanite-like ≠ 商标 Nanite |

先前（W24/W25 文档）Godot 内核≈100% / Unity 68–78% / UE5 40–50% **已撤回为虚高**。

## 3. 分域粗表（渲染向）— 诚实区间

| 功能域 | vs Godot | vs Unity | vs UE5 | 本引擎要点 / Cap |
|---|---:|---:|---:|---|
| 基础 3D 光栅 | **75–85%** | 55–65% | 40–50% | Forward+ / CSM / IBL（规模不如三家） |
| 后处理 | **70–80%** | 55–65% | 35–45% | GTAO / FXAA / LUT / fog / SSR |
| 动态 GI | **45–55%** | 30–40% | 15–25% | CascadeGi 默认；RTXGI linked 仍为合成 atlas |
| 光追 | **25–40%** | 15–25% | 8–15% | DXR 示范；VK 软影×0.92 暗化桩；反射 64×36 合成缓冲 |
| GPU Driven | **40–55%** | 30–40% | 15–25% | VG：连续 LOD + Indirect；**cull 为 CPU CS 合同** |
| 超分 | **20–35%** | 15–25% | 10–18% | 无库 SKIP→bilinear；linked 仍为 CPU nearest（非 NGX EvaluateFeature） |
| 材质 / 粒子 | **55–70%** | 40–50% | 25–35% | detail/triplanar；GPU 粒子 CS |
| 2D | **80–88%** | 55–65% | 40–50% | CanvasItem 树 / Camera2D / UV 合批 / Tile 真画 / Anim2D 子集；无 Navigation2D·Spine |
| 物理 | **75–82%** | 55–65% | 40–50% | Physics2D（builtin AABB Godot 体语义）+ Jolt 真关节/ShapeCast/层/Area；Vehicle/Shatter 仍教学桩 |
| 双后端纪律 | **80–90%** | 70–85% | 60–75% | **相对强项**：诚实 SKIP、双端 lit 合同 |

**不进本封板（整引擎尺子）：** 音频 / 引擎内脚本·复制 / 网络同步 / 平台。编辑器在独立 `editor/`。

## 4. W25 合同水位 / 仍外置

**合同已收口（勿读成产品等价）：**

- VK↔D3D12：半分辨率软影 mask / 反射缓冲→SSR **同 Upload 合同**（VK 路径可为暗化/合成 stand-in）。
- NGX/RTXGI：有头+库时 `ENGINE_*_EVALUATE_LINKED` + 无库 SKIP；**真厂商 Evaluate 尚未接线**。
- VG：连续误差 LOD / 驻留 / Indirect / SW splat；**非** 真 GPU cull CS、**非** UE Nanite。
- 外挂编辑器：中小关卡 ≈95%（见 editor 文档）。

**仍外置：** Lumen / Frame Generation / XeSS / mac / C17 / 引擎内脚本·复制 / 宣称 UE Nanite / GDScript·材质节点图 / 全屏路径追踪。

## 5. 修订记录

| 日期 | 水位 | 变更 |
|---|---|---|
| 2026-08-20 | W20–W24 | 见看板（当时分域 vs Godot 标 100%，现已修订） |
| 2026-08-20 | **W25** | VK RT 合同；evaluate 链接纪律；VG 加深；editor ADR 0002（ADR 0048） |
| 2026-08-21 | **审计修订** | 撤回 Godot 内核 100%；按源码重打分域；同步 editor ≈95% |
| 2026-08-21 | **W26 / ADR 0049** | 2D Canvas/Camera/UV 批 + Physics2D + Jolt 加深；内核 vs Godot ≈60–72%；2D≈80–88%；物理≈75–82%（非 100%） |
