# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 主线水位（截至 W-vk-parity 大波）

| 层 | 状态 |
|---|---|
| **W-auto-1** | **已收口**（`061e478`） |
| **W-auto-2** | **已收口**（`7603a4d`） |
| **W-env-sky** | **已收口**（`7603a4d`） |
| **HUD 清理** | **已收口**（`101c364`） |
| **W-vk-parity** | **已收口**：几何/贴图/天空/点光/UI/Debug/scale/IBL |
| **T-c4-strict** | **可用**：`--strict` / CI `-StrictParity`；主门禁仍默认非硬堵 |
| **W-test-polish** | **已收口**：C7 加载 fuzz + Q5 ROI |
| **W-gi-deepen** | **已收口**：密网格 + 增量更新 + 三线性采样 |

```text
安全基线：见 git tip（本波落地后）
```

---

## 约束

- **不扩** Harness 命令、不扩 MCP、CI 不依赖 MCP
- **不静默安装** 系统 OpenSSL
- C4：默认 ROI；超阈 `[REGRESSION-NOTED]`；`-StrictParity` / `--strict` 才 FAIL
- 大气 / Linux / SoftBody 实现：仍排队
- **不要动 `editor/`**（并行会话）

验收总闸：`ci_headless.ps1 -Golden`（可选 `-StrictParity`）

---

## Doing

（空 — 本大波已收口）

---

## Todo（波次）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P1 | W-phys-soft | 薄 SoftBody/Cloth：`IPhysicsWorld` + Jolt + Demo | KNOWN_GAPS **C22** |
| 闸门 | T-https-openssl-on | 授权装 SDK 后 HTTPS loopback | 现 SKIP |
| 后置 | T-bindless-stable | Bindless 热路径 | 黄金图曾漂 |
| P3 | T-q4-warp / Linux | WARP / M18 | |
| P3 | T-c05-atmosphere | 大气 / 体积云 | C05 |

### 产品轨（不进本板实现波）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| 产品 P0 | W-game-kit | GK0–GK3 | **仅文档**；下令后再开工 |
| 产品 P1 | W-prefab | GK4 Prefab + Manifest | |
| 引擎候选 | C20 | 轻量 CLI | 非视口编辑器 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `101c364`（本波前） |

验证：`ci_headless.ps1 -Golden`

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **W-vk-parity** | Vulkan lit 贴图/mesh/天空/UI/Debug/点光/IBL；Sandbox 解禁 |
| **T-c4-strict** | ROI + `--strict` / `-StrictParity` |
| **W-test-polish** | C7 unit + Q5 ROI |
| **W-gi-deepen** | ProbeVolume 增量 + 三线性；Sandbox F1 叠加 ambient |
| **板梳理** | 基线 `101c364`；布料 C22 |
| **HUD 清理 / W-env-sky / W-auto-*** | 既有 |
