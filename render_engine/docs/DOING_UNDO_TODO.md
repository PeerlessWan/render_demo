# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W9

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | render_engine（五主题+尾巴）+ 学习轨 + DOC_AUDIT |
| **口径** | [ADR 0036](learn/adr/0036-mega-w9-deepen.md)、[DOC_AUDIT](learn/DOC_AUDIT.md) |

## Mega-W8

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **tip** | `0942719` |
| **口径** | [ADR 0035](learn/adr/0035-mega-w8-deepen.md) |

## 主线水位

| 层 | 状态 |
|---|---|
| **W0–W8** | 已收口 |
| **Mega-W9** | **已收口**：C02 CS/range、C08 MS、VT/HLOD/流式、GI/RT、蒙皮主路径、热更/QUIC/Linux、尾巴、学习轨、文档审计 |

```text
当前波：Mega-W9 收口
外置：Nanite / 真 DDGI / XR / 材质节点图 / 蓝图 / mac·移动
验收：engine_unit_tests + learn samples + DOC_AUDIT
```

## Doing

| ID | 项 |
|---|---|
| — | （无进行中加深波） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 后置 | C18 | XR（定位不做） |
| 后置 | G17 | 材质节点图（定位不做进引擎） |
| 钉死 | C17 | 多窗口/多 GPU：见 [C17_MULTI_WINDOW.md](C17_MULTI_WINDOW.md) |
| 闸门 | MsQuic/OpenSSL | 本机已装才启用 |

## Undo

| 标签 | 值 |
|---|---|
| W8 推送 | `0942719` |
| W7 推送 | `99542b6` |

## Done（近期）

| 项 | 说明 |
|---|---|
| **Mega-W9** | ADR 0036；test_m32–m33；学习轨 CH00–CH36；DOC_AUDIT |
| **Mega-W8** | ADR 0035；test_m29–m31 |
| **W4–W7** | ADR 0033–0034 |
