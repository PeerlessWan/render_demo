# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空） | 本档已收口 | 见 Done |

### 本档无 vendor 100% 勾选（已完成）

| 勾选 | 项 | 说明 |
|---|---|---|
| [x] | Q1 确定性截帧 | 固定 dt；关 TAA/Jitter；冻粒子/物理；golden 稳定 |
| [x] | Q2 VK 真 Readback | `--backend=vulkan --gpu-headless` 可出 `.rgba` |
| [x] | C1 Validation CI | `ci_headless -Validation`；无层 SKIP |
| [x] | HiZ/Cull GPU CS | CS 或等价 GPU 路径可测；Sandbox 可消费 |
| [x] | VK post 中间 RT | lit→tonemap→`scene_color` blit（中间 RT） |
| [x] | Bindless Feature 最小路径 | Tier≥2 可查询 + 堆槽索引探针；非全材质迁移 |
| [x] | Learn 2A README | 全部 `samples/learn/*/README.md` 含完整教学块 |
| — | 真 RmlUi / OpenSSL / Linux | **外置**（不进本档） |

---

## Todo（下一档 / 阻塞）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P2 | T-rmlui-real | 真 RmlUi | **阻塞**：需 vendor |
| P2 | T-http-tls | OpenSSL HTTPS | **阻塞**：缺 SDK |
| P3 | T-linux-m18 | Linux + Vulkan | 外置（见 LINUX_VULKAN.md） |
| P3 | T-ground-slab | 悬浮浅色层切物体 | **搁置** |
| P3 | T-m14-bindless-full | 全 Bindless 迁移 | 本档仅 Feature 最小路径 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `4b2f5f5`（四轨 100%） |
| 本档回退 | 还原 Q1–C1、VK 读回/post RT、GPU HiZ CS、Bindless Feature、learn README |

验证：`scripts/ci_headless.ps1 -Golden`；`-Validation`（可选）；unit；Sandbox / learn

---

## Done（近期）

| 项 | 说明 |
|---|---|
| W-m100-nv | 无 vendor 100% + Learn 2A：Q1–C1、VK 读回/post RT、HiZ Cull CS、Bindless Feature、learn README |
| Track A–D | 四轨 100%（产品 / GPU-driven / VK 矩阵 / 自动化底座） |
| W-a-test / golden / matrix | 已落地；Harness 冻结；MCP 不进门禁 |
| T-ibl-real / T-gpu-headless / learn 阶梯代码 | 已有 |
