# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W11

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | Win VK 对齐 + Linux X11/VK + CC0 glTF（**不含** game_kit/editor） |
| **口径** | [ADR 0038](learn/adr/0038-mega-w11-parity.md) |

## Mega-W10

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **口径** | [ADR 0037](learn/adr/0037-mega-w10-deepen.md) |

## 主线水位

```text
当前波：Mega-W11 收口（引擎 only）
外置：Nanite / 真 NVIDIA DDGI / XR / 节点图 / 蓝图 / mac / C17
禁止本波改动：game_kit/、editor/（他会话）
```

## Doing

| ID | 项 |
|---|---|
| — | （无进行中加深波） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 他会话 | game_kit / editor | 不在本波 |
| 后置 | Wayland | LINUX 钉死 |
| 钉死 | C17 | 多窗口不实装 |

## Undo

| 标签 | 值 |
|---|---|
| W11 | ADR 0038 |
| W10 | ADR 0037 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **Mega-W11** | ADR 0038；VK tile/skin/MS/RT；Linux X11；Kenney glTF；test_m36 |
| **Mega-W10** | ADR 0037 |
| **Mega-W9** | ADR 0036 |
