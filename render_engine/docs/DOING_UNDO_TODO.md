# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 主线水位（截至 Win 双后端 100% 收口）

| 层 | 状态 |
|---|---|
| **W-auto-1** | **已收口**（`061e478`） |
| **W-auto-2** | **已收口**（`7603a4d`） |
| **W-env-sky** | **已收口**（`7603a4d`） |
| **HUD 清理** | **已收口**（`101c364`） |
| **W-vk-parity** | **已收口**：几何/贴图/天空/点光/UI/Debug/scale/IBL |
| **W-win-dual-100** | **已收口（本口径）**：VK 全栈 post + GPU 实例/Cull/Indirect + 探针/IBL 分槽 + SoftBody + Sandbox EN/ZH；Bindless 热路径 Feature 门控 |
| **T-c4-strict** | **可用**：`--strict` / CI `-StrictParity`；主门禁仍默认非硬堵 |
| **W-test-polish** | **已收口**：C7 加载 fuzz + Q5 ROI |
| **W-gi-deepen** | **已收口**：密网格 + 增量更新 + 三线性采样 |

```text
安全基线：见 git tip（Win 双后端 100% 波）
口径说明：docs/VULKAN_PARITY.md（不含 Linux/大气/editor）
```

---

## 约束

- **不扩** Harness 命令、不扩 MCP、CI 不依赖 MCP
- **不静默安装** 系统 OpenSSL
- C4：默认 ROI；超阈 `[REGRESSION-NOTED]`；`-StrictParity` / `--strict` 才 FAIL
- 大气 / Linux：**本口径外**，仍排队
- **不要动 `editor/`**（并行会话）

验收总闸：`ci_headless.ps1 -Golden`（可选 `-StrictParity`）

---

## Doing

（空 — Win 双后端 100% 波已收口）

---

## Todo（波次）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| 闸门 | T-https-openssl-on | 授权装 SDK 后 HTTPS loopback | 现 SKIP |
| P3 | T-q4-warp / Linux | WARP / M18 | 口径外 |
| P3 | T-c05-atmosphere | 大气 / 体积云 | C05；口径外 |

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
| 安全基线 | 见 `VULKAN_PARITY.md` 收口前 tip |

验证：`ci_headless.ps1 -Golden`

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **W-win-dual-100** | Sandbox i18n；VK post 全栈；GPU 实例；探针/IBL 分槽；SoftBody；Cull/Indirect；Bindless Feature 门控；文档 100% 口径 |
| **W-phys-soft** | `IPhysicsWorld` SoftBody + Jolt；builtin SKIP；Sandbox DebugDraw |
| **T-bindless-stable** | 默认 classic；`bindless_hot_path` opt-in；VK SKIP |
| **W-vk-parity** | Vulkan lit 贴图/mesh/天空/UI/Debug/点光/IBL；Sandbox 解禁 |
| **T-c4-strict** | ROI + `--strict` / `-StrictParity` |
| **W-test-polish** | C7 unit + Q5 ROI |
| **W-gi-deepen** | ProbeVolume 增量 + 三线性；Sandbox F1 叠加 ambient |
| **HUD 清理 / W-env-sky / W-auto-*** | 既有 |
