# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 主线水位（截至板梳理）

| 层 | 状态 |
|---|---|
| **W-auto-1** | **已收口**（`061e478`） |
| **W-auto-2** | **已收口**（`7603a4d`）：C6；C4 薄对标；OpenSSL SKIP |
| **W-env-sky** | **已收口**（`7603a4d`）：真 HDR IBL+天空盒；Sandbox 开关 |
| **HUD 清理** | **已收口**（`101c364`）：去掉左下 Retained HUD |

```text
安全基线：101c364
```

---

## 约束

- **不扩** Harness 命令、不扩 MCP、CI 不依赖 MCP
- **不静默安装** 系统 OpenSSL；`T-https-openssl-on` 需你授权
- C4 默认 `[REGRESSION-NOTED]`；`--strict` 仅在 `W-vk-parity` 后另开
- 大气（C05）、Linux、Q4 WARP：P3，不挡 GI / 薄 SoftBody 排队
- 布料/软体：**薄** `IPhysicsWorld` + Jolt（C22 / [ADR 0029](learn/adr/0029-physics-softbody-boundary.md)）；不做服装管线/破坏/轮胎产品化

验收总闸：`ci_headless.ps1 -Golden`

---

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| **W-vk-parity** | Win Vulkan lit/post/sky 对标 | 压低 C4 RMSE | 同机 C4 明显下降；`-Golden` 绿；严 C4 仍默认关 |

> 本轮板梳理已落地文档；**实现波未开工**——下令「开 W-vk-parity」后再改代码。

---

## Todo（波次）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P0 | W-vk-parity | 见 Doing | 默认下一实现波 |
| P1 | W-test-polish | C7 加载 fuzz ± Q5 ROI（忽略 Perf/橙块） | 不扩 Harness |
| P1 | W-gi-deepen | ProbeVolume 加深；与 IBL/Lightmap 共存；可关 | G14；非完整 DDGI |
| P1 | W-phys-soft | 薄 SoftBody/Cloth：`IPhysicsWorld` + Jolt + Demo | KNOWN_GAPS **C22** |
| 闸门 | T-https-openssl-on | 授权装 SDK 后 HTTPS loopback | 现 SKIP |
| 后置 | T-c4-strict | VK 收紧后可选严 C4 | 现 RMSE≈120 |
| 后置 | T-bindless-stable | Bindless 热路径 | 黄金图曾漂 |
| P3 | T-q4-warp / Linux | WARP / M18 | |
| P3 | T-c05-atmosphere | 大气 / 体积云 | C05 |

### 近端工程债（摘要）

HTTPS · C4/VK · Bindless · VK sky/ImGui/Debug · Q4/Q5/C7 · Linux · 可选橙块清理

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `101c364` |

验证：`ci_headless.ps1 -Golden`

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **板梳理** | 基线 `101c364`；波次化 Todo；GI / 薄 SoftBody 入队；布料从「不做」上调 C22 |
| **HUD 清理** | `101c364` 去掉左下 Retained HUD |
| **W-env-sky** | Environment；真 HDR；天空盒；黄金/哈希重批 |
| **W-auto-2** | C6 + C4 薄对标；OpenSSL SKIP |
| W-auto-1 | C3/C5；Cull；VK local；RmlUi；HTTPS 用例 |
