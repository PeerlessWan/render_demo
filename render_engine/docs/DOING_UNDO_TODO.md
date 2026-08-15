# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 主线水位（截至 Engine 100% 加深波 W0–W3）

| 层 | 状态 |
|---|---|
| **W-win-dual-100** | **已收口（本口径）** |
| **W0 Win polish** | **已收口**：CSM Poisson/tile clamp/法线 bias；QualityTier 拉开 atlas/距离/植被/DoF |
| **W1 里程碑加深** | **已收口**：spot、Character 胶囊、glTF skin、Profiler/Lightmap、DXR 门控、Tiled、地形水草 |
| **W1b** | **已收口**：Trail、分辨率超分、HTTPS 提示、QUIC ADR SKIP、Retained HUD；RmlUi 外置口径 |
| **W2** | **已收口**：Tilemap→Sprite、SkeletonClip2D、Probe+Lightmap 共存 |
| **W3 / M26** | **已收口**：ADR 0032；C01 Forward+、C02 8 灯、C10 状态机、C08 MeshShader SKIP、C04 vignette/grain、C16 热重载 Poll、C20 content_lint |

```text
安全基线：见 git tip（Engine 100% 加深波）
口径说明：docs/VULKAN_PARITY.md（不含 Linux/大气/editor/game_kit）
```

---

## 约束

- **不扩** Harness 命令、不扩 MCP、CI 不依赖 MCP
- **不静默安装** 系统 OpenSSL / MsQuic
- C4：默认 ROI；超阈 `[REGRESSION-NOTED]`；`-StrictParity` / `--strict` 才 FAIL
- 大气 / Linux：**本口径外**，仍排队
- **不要动 `editor/`**（并行会话）

验收总闸：`ci_headless.ps1 -Golden`（可选 `-StrictParity`）

---

## Doing

（空 — Engine 100% 加深 W0–W3 已收口）

---

## Todo（波次）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| 闸门 | T-https-openssl-on | 授权装 SDK 后 HTTPS loopback | 现 SKIP；不静默装 OpenSSL |
| P2 | T-csm-pillar-shimmer | CSM 柱面主观残留（可选再录盘） | W0 已落地 Poisson 等；见笔记 |
| P3 | T-q4-warp / Linux | WARP / M18 | 口径外 |
| P3 | T-c05-atmosphere | 大气 / 体积云 | C05；口径外 |
| P3 | T-quic-msquic | MsQuic 捆绑（需另批） | ADR 0031 SKIP |
| 后置 | C03/C06/C07/C09/C11/G13… | §4 未开项 | 见 KNOWN_GAPS |

### Deferred 笔记 · `T-csm-pillar-shimmer`

- **现象**：绿柱垂面互投阴影；`captures/sandbox_20260815_170746` 曾见成对 ΔL ≈ ±5～8。
- **W0 已落地**：origin texel-snap、半径 0.5 量子、cascade overlap、log 偏置 0.75、Poisson PCF、tile clamp、法线/斜率 bias、宽 cascade blend。
- **验证**：`captures/_analyze_dump.py` + 可选绿 mask 脚本；只开 Shadows 再 F5。

### 产品轨（不进本板实现波）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| 产品 P0 | W-game-kit | GK0–GK3 | **仅文档**；下令后再开工 |
| 产品 P1 | W-prefab | GK4 Prefab + Manifest | |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | 加深波前 tip；验证 `ci_headless.ps1 -Golden` |

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **Engine 100% 加深 W0–W3** | 见上方主线水位 |
| **W-win-dual-100** | 既有双后端口径 |
| **ADR 0030–0032** | DXR 范围 / QUIC SKIP / M26 Forward+ |
| **content_lint** | C20 Manifest CLI |
