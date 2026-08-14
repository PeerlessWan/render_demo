# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空）P2 可测切片已落地，待选下一档 |  |  |

---

## Todo（下一档）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P2 | T-rmlui-real | 真 RmlUi | **阻塞**：需 vendor |
| P2 | T-http-tls | OpenSSL HTTPS | **阻塞**：缺 SDK |
| P3 | T-vk-parity | Vulkan 追平 | 暂缓 |
| P3 | T-ground-slab | 悬浮浅色层切物体 | **搁置** |
| P3 | T-m14-full | 真多线程 Execute / 全 Bindless 迁移 | 本轮仅骨架 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `9db8df0`（含 HDR lit + 地板缓解） |
| 上一基线 | `8d27fab`（Perspective Z∈[0,1]） |
| 本档回退 | 还原 gpu_headless / TAA jitter / ReflectionProbe / Feature override |

验证：`scripts/ci_headless.ps1`；`engine_unit_tests`（含 GPU 回归）；Sandbox `--gpu-headless --headless_frames=4`

---

## Done（近期）

| 项 | 说明 |
|---|---|
| T-gpu-headless | D3D12 offscreen + 读回断言 + `ci_headless.ps1` |
| T-taa-mv | prev VP + 深度重投影 TAA + Halton jitter；Medium/High 默认开 |
| T-m13-dynref | `ReflectionProbe` Cubemap 上传 + lit 采样 |
| T-m14-gates | Feature override、SubmitConfig、HDR10 尝试、bindless heap 余量 |
| T-hdr-scene | Lit → `R16G16B16A16` + tonemap 回 LDR（`9db8df0`） |
| T-ground-mitigate | 细分地面 / 单级联 / 局部灯收敛 / ClipDistance（假层仍在） |
| T-ground-z | Perspective → D3D Z[0,1]（`8d27fab`） |
| T-wheel-crash | D3D12 帧中 VB / WantCapture（`a638fdc`） |
