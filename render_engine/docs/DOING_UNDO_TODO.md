# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W12–W15（水位弱项产品化 A+C）

| 项 | 值 |
|---|---|
| **状态** | **进行中** |
| **范围** | 引擎 only；边界见 [ADR 0039](learn/adr/0039-waterline-productization-a-c.md) |
| **外置不变** | Nanite / 真 NVIDIA DDGI / XR / 节点图 / 蓝图 / mac / C17 / 引擎内复制 |

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
当前波：W12–W15 水位弱项产品化（A+C）
外置：Nanite / 真 NVIDIA DDGI / XR / 节点图 / 蓝图 / mac / C17 / 引擎内复制
本计划可改：Wayland（目标）、VK bindless（解除钉死 SKIP）
禁止：把复制同步塞进 engine/net；不改 game_kit/、editor/（除非他会话）
```

## Doing

| ID | 项 |
|---|---|
| — | （W12–W15 代码已落地；验收/实机 Linux 持续） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 他会话 | game_kit / editor | 玩法壳 / 复制不在本计划 |
| 钉死 | C17 | 多窗口不实装 |
| 钉死 | Nanite / 真 DDGI | 外置 |
| 后续 | FSR/DLSS SDK vendor | `fetch_fidelityfx` + NGX 真接线 |
| 后续 | Wayland xdg-shell present | 现为 display 探测 + X11 present |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 他会话 | game_kit / editor | 玩法壳 / 复制不在本计划 |
| 钉死 | C17 | 多窗口不实装 |
| 钉死 | Nanite / 真 DDGI | 外置 |

## Undo

| 标签 | 值 |
|---|---|
| W11 | ADR 0038 |
| W10 | ADR 0037 |
| W12–W15 边界 | ADR 0039 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **Mega-W11** | ADR 0038；VK tile/skin/MS/RT；Linux X11；Kenney glTF；test_m36 |
| **W12–W15** | ADR 0039 水位弱项产品化（A+C）首批落地 |
| **Mega-W10** | ADR 0037 |
| **Mega-W9** | ADR 0036 |
