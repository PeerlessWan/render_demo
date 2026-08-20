# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W24（分域 vs Godot ≈100%）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0047](learn/adr/0047-w24-godot-domain-100.md) |
| **水位** | 渲染向分域 vs Godot 一律约 100%；光追=Win D3D12 |
| **仍外置** | Lumen / FG / XeSS / mac / C17 / 引擎内复制；整引擎音频/编辑器/网络 |
| **单测** | 220 passed / 0 failed |

## Mega-W23（缺口 + Nanite-like + 真 DDGI）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0046](learn/adr/0046-w23-nanite-ddgi-gaps.md) |
| **单测** | 215 passed / 0 failed |

## 主线水位

```text
当前波：W24 已收口（ADR 0047）— 分域 vs Godot ≈100%
光追口径：Win D3D12/DXR；Vulkan RT 有意差
外置：Lumen / FG / XeSS / mac / C17 / 引擎内复制
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

## Undo

| 标签 | 值 |
|---|---|
| W24 | ADR 0047 |
| W23 | ADR 0046 |
| W22 | ADR 0045 |
| W21 | ADR 0044 |
| W20 | ADR 0043 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **W24** | 分域 vs Godot ≈100%；DXR 产品软影/反射；VG 热路径；Character/Trigger/Vehicle；220 unit |
| **W23** | VirtualGeometry；RTXGI；缺口收口；215 unit |
| **W22** | Godot 内核 ≈100%；208 unit |
