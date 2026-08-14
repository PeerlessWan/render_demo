# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 主线水位（截至 W-env-sky 本地）

| 层 | 状态 |
|---|---|
| **W-auto-1** | **已收口**（`061e478`） |
| **W-auto-2** | **本地绿待 commit**：C6 着色器哈希进 `-Golden`；C4 薄对标（超阈记回归）；OpenSSL **本机未装 → SKIP**（不静默装系统包） |
| **W-env-sky** | **本地绿待 commit**：真 HDR→IBL+天空盒；Sandbox 天空绘制与 F1 开关；黄金图/哈希已重批 |

```text
安全基线：061e478（下一 commit 后更新）
```

---

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空） | W-env-sky 待收波报告 | 见 Done |

---

## Todo（波后）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P1 | T-https-openssl-on | 本机安装 OpenSSL 后 `ENGINE_WITH_OPENSSL=ON` 跑通 loopback | 需用户授权装 SDK |
| P1 | T-c4-strict | 收紧 VK lit/post 后对 C4 开 `--strict` | 现 RMSE≈109 |
| P2 | T-bindless-stable | Bindless 热路径 | 黄金图曾漂 |
| P3 | T-q4-warp / Linux | 后置 | |
| P3 | T-c05-atmosphere | 大气 / 体积云 / 天气 | KNOWN_GAPS C05 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `061e478` |

验证：`ci_headless.ps1 -Golden`

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **W-env-sky** | Environment 加深；`ibl_baker` 真读 Poly Haven 1K HDR；`skybox.hlsl` + D3D12 Draw；Sandbox 默认开 + ImGui 开关；C6/黄金/matrix 重批 |
| **W-auto-2** | C6 `shader_hashes.json`；C4 `run_backend_parity.py`（记对标）；OpenSSL SKIP |
| W-auto-1 | C3/C5；Cull compact；VK local；RmlUi；HTTPS 用例 |
| W-sandbox-perf / W-abc-wave | 见历史 |
