# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W8

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | render_engine + game_kit + editor 冒烟 |
| **口径** | [ADR 0035](learn/adr/0035-mega-w8-deepen.md) |

## 主线水位

| 层 | 状态 |
|---|---|
| **W0–W7** | 已收口 |
| **Mega-W8** | **已收口**：C02 tile 热路径、C08 meshlet、C06 VT、MsQuic、天气/FFT海/浮力、动画/IES/镜头、VK蒙皮/热更/2D、GK热重载/GK5、ED冒烟 |

```text
当前波：Mega-W8 收口
外置：HLOD / XR / 材质节点图 / 蓝图 / mac
验收：engine_unit_tests + game_kit_tests + editor_smoke_tests
```

## Doing

| ID | 项 |
|---|---|
| — | （无进行中加深波） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 后置 | C07/C17/C18 | HLOD / 多 GPU / XR |
| 后置 | G17 | 材质节点图（定位不做进引擎） |
| 闸门 | MsQuic/OpenSSL | 本机已装才启用 |

## Undo

| 标签 | 值 |
|---|---|
| W7 推送 | `99542b6` |

## Done（近期）

| 项 | 说明 |
|---|---|
| **Mega-W8** | ADR 0035；test_m29–m31；game_kit/editor 冒烟 |
| **W4–W7** | ADR 0033–0034 |
