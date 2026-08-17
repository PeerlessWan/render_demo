# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 解封（Engine reopen · M27+）

| 项 | 值 |
|---|---|
| **状态** | **W4–W7 已收口** |
| **封板基线** | `1700a71`；W4–W6 `e92758c` |
| **边界** | **不动** `editor/` / `game_kit/` / `games/` |
| **口径** | 见 [ADR 0034](learn/adr/0034-m27-w7-parity-deepen.md) |

---

## 主线水位

| 层 | 状态 |
|---|---|
| **W0–W6** | **已收口** |
| **W7 对标加深** | **已收口**：C05 云/雾 · C03 IES · C14 世界字 · C04 色差 · C12 CS 蒙皮 · DXR 真光线 |

```text
当前波：W7 收口
禁止：editor / game_kit / games
不做：C06 VT / C07 HLOD / C11 IK / G13 Path2D / MsQuic
验收：engine_unit_tests（含 test_m28）
```

---

## 约束

- **不扩** Harness 命令、不扩 MCP、CI 不依赖 MCP
- **不静默安装** 系统 OpenSSL / MsQuic
- C4：默认 ROI；超阈 `[REGRESSION-NOTED]`；`-StrictParity` / `--strict` 才 FAIL
- **不要动 `editor/` / `game_kit/`**

验收总闸：`ci_headless.ps1 -Golden`（可选 `-StrictParity`）

---

## Doing

| ID | 项 |
|---|---|
| — | （无进行中加深波） |

---

## Todo

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| 闸门 | T-https-openssl-on | 有 OpenSSL 才启用 HTTPS | 不静默安装 |
| 后置 | C06/C07/C17/C18 | VT / HLOD / 多窗 / XR | 本轮不排 |
| 后置 | G13 / C11 | 矢量 / IK | 本波不做 |

### Deferred 笔记 · `T-csm-pillar-shimmer`

- W0 snap/Poisson + W4 近 cascade / 接收平面 bias；绿 mask 验收可按需复录。

### 产品轨（本波不做）

| ID | 项 |
|---|---|
| W-game-kit / editor | 放一边 |

---

## Undo

| 标签 | 值 |
|---|---|
| 解封前基线 | `1700a71` / 封板 `284a336` |
| W4–W6 收口 | `e92758c` |

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **W7** | 大气云雾 · IES · 世界字 · 色差 · D3D12 CS 蒙皮 · DXR DispatchRays；ADR 0034 |
| **W4–W6** | 画质 / 平台媒体 / 场景规模；ADR 0033 |
| **ADR 0030–0034** | DXR / QUIC / M26 / W6 / W7 |
