# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。  
> 对标数字以 [ENGINE_VS_MAINSTREAM.md](ENGINE_VS_MAINSTREAM.md) **2026-08-21 W26/ADR 0049**为准（勿再读历史 ADR 里的 Godot≈100% 为现行宣称）。

## Mega-W26（Godot 2D API + Physics2D + 3D 加深）

| 项 | 值 |
|---|---|
| **状态** | **API 已落地**；对标诚实复审 |
| **范围** | [ADR 0049](learn/adr/0049-godot-2d-physics-api.md) |
| **水位** | CanvasItem/Camera2D/UV 批；`IPhysicsWorld2D` builtin；Jolt 真关节/ShapeCast/层/Area；`sample_41_platform_2d` |
| **明确不做** | Navigation2D / Spine / GDScript / 载具轮胎产品化 / SoftBody 服装 |
| **单测** | **233 passed / 0 failed**（含 W26） |
| **现行对标** | 渲染内核 vs Godot ≈60–72%；2D ≈80–88%；物理 ≈75–82% |

## Mega-W25（VK + NGX/RTXGI + VG + 编辑器）

| 项 | 值 |
|---|---|
| **状态** | **合同已收口**（产品对标另见 ENGINE_VS） |
| **范围** | [ADR 0048](learn/adr/0048-w25-vk-ngx-vg-editor.md) + editor ADR 0002 |
| **水位** | VK/DXR **缓冲合同**；NGX/RTXGI **链接+SKIP 纪律**（非真 Evaluate）；VG **CPU cull 合同**；编辑器 Godot 关卡 ≈95% |
| **仍外置** | Lumen / FG / XeSS / mac / C17 / 引擎内复制；视频硬解有意差 |
| **单测** | 历史 226；现以 W26 板为准 |
| **现行对标** | 见 W26 / ENGINE_VS |

## Mega-W24（分域 vs Godot — 历史宣称）

| 项 | 值 |
|---|---|
| **状态** | **历史收口**；分域「一律 ≈100%」**已由 2026-08-21 审计撤回** |
| **范围** | [ADR 0047](learn/adr/0047-w24-godot-domain-100.md) |
| **单测** | 220 passed / 0 failed（当时） |

## 主线水位

```text
当前波：W26 API 已落地（ADR 0049）
对标：ENGINE_VS 2026-08-21 W26（Godot 内核 ≈60–72%，2D≈80–88%，物理≈75–82%；非 100%）
外置：Lumen / FG / XeSS / mac / C17 / Navigation2D / Spine / 引擎内复制
```

## Doing

| ID | 项 |
|---|---|
| — | （空） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 他会话 | genre_kits / games | 新品类与内容 |
| 钉死 | C17 / Lumen / FG / XeSS | 外置 |
| 可选加深 | 真 NGX Evaluate / 真 RTXGI Update / 真 GPU VG cull / VK TraceRays 产品帧 | 非本看板必做 |

## Undo

| 标签 | 值 |
|---|---|
| W25 | ADR 0048 |
| W24 | ADR 0047（对标数字已修订） |
| W23 | ADR 0046 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **W25** | VK/DXR 软影·反射合同；NGX/RTXGI 链接纪律；VG 加深；editor ≈95%；226 unit 注册 |
| **2026-08-21** | 审计修订 ENGINE_VS：撤回 Godot 内核 100% |
| **W24** | 当时分域 vs Godot 标 100%（已撤回为虚高） |
| **W23** | VirtualGeometry；RTXGI Bind；215 unit |
