# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 解封（Engine reopen · M27+）

| 项 | 值 |
|---|---|
| **状态** | **W4→W5→W6 已收口**（画质 → 平台/媒体 → 场景规模） |
| **封板基线** | `1700a71`（可回退） |
| **边界** | **不动** `editor/` / `game_kit/` / `games/` |
| **口径** | Win D3D12+Vulkan 为主；见 [ADR 0033](learn/adr/0033-m27-w6-scene-scale.md) |

---

## 主线水位

| 层 | 状态 |
|---|---|
| **W0–W3** | **已收口**（封板快照） |
| **W4 画质债** | **已收口**：CSM 柱影、内置超分、DXR stub、C02 集群灯、C05 大气起步 |
| **W5 平台/媒体** | **已收口**：LINUX.md / HTTPS 说明 / VA stub；QUIC 维持 ADR 0031 SKIP |
| **W6 场景规模** | **已收口**：GI 加密、水面、混合树、GPU 蒙皮 stub、Meshlet 门控、热重载 PSO 请求 |

```text
当前波：W4–W6 收口（M27+）
禁止：editor / game_kit / games
验收：engine_unit_tests（含 test_m27）+ ci_headless.ps1 -Golden
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
| — | （无进行中加深波；下一批另开） |

---

## Todo

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| 闸门 | T-https-openssl-on | 有 OpenSSL 才启用 HTTPS | 不静默安装 |
| 后置 | C06/C07/C17/C18 | VT / HLOD / 多窗 / XR | 本轮不排 |
| 后置 | C03/C14/G13 | IES / 世界字 / 矢量 | 中低优先 |

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

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **W4** | CSM / 超分 / DXR stub / C02≤16 / C05 EvalSkyColor |
| **W5** | LINUX.md · HTTPS 文档 · VA stub · QUIC SKIP |
| **W6** | RefineDensity · AnimateWater · SampleBlend · GpuSkin stub · Meshlet 门控 · PSO rebuild 请求；ADR 0033 |
| **Engine 封板→解封** | M27+ 加深完成一轮 |
| **W0–W3** | 100% 加深收口 |
| **ADR 0030–0033** | DXR / QUIC / M26 / W6 |
