# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空）四轨 100% 已收口 |  |  |

---

## Todo（下一档 / 阻塞）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P2 | T-rmlui-real | 真 RmlUi | **阻塞**：需 vendor |
| P2 | T-http-tls | OpenSSL HTTPS | **阻塞**：缺 SDK |
| P3 | T-linux-m18 | Linux + Vulkan | 外置（见 LINUX_VULKAN.md） |
| P3 | T-ground-slab | 悬浮浅色层切物体 | **搁置** |
| P3 | T-m14-bindless-full | 全 Bindless 迁移 | 非本口径必须 |
| P3 | T-hiz-gpu-cs | HiZ/Cull 真 GPU CS | CPU 合同已落地；可换 GPU CS |
| P3 | T-vk-post-full | Vulkan 全 SPIR-V post 栈 | 子集 tonemap 已有；中间 RT 可扩 |
| P3 | T-test-deepen | 自动化测试加深 | [PLAN §3.1](PLAN.md)：Q1 确定性截帧 → Q2 VK 真读回 → C1 Validation CI |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `4f68e14`（deepen）或产品波次 `0354878` |
| 本档回退 | 还原 HiZ/Cull、VK post/local shadow、MCP spawn、golden/matrix |

验证：`scripts/ci_headless.ps1 -Golden`；unit；Sandbox `--gpu-headless`；`--harness-stdio`；`sandbox_mcp` + `ENGINE_SANDBOX_EXE`

---

## Done（近期）

| 项 | 说明 |
|---|---|
| Track A | D3D12 产品主路径：IBL/CSM/点光影/TAA/探针 GPU/post；gpu-headless |
| Track B | HiZ + Cull→IndirectArgs + Sandbox 1k 实例化热路径（无 Bindless） |
| Track C | VK SPIR-V post 子集、局部影 atlas、实例化/Indirect Feature；矩阵更新 |
| Track D | Harness 真接线；MCP 控真机；golden + matrix smoke；**此后 Harness 冻结 / MCP 不扩**（PLAN §3.21） |
| W-a-test | TESTING/看板；黄金图脚本；`ENGINE_GOLDEN_DUMP` |
| W-b1-inst | D3D12 `DrawLitInstanced` + learn22 |
| W-b2-probe | `CaptureReflectionProbeGpu` + CPU fallback |
| W-b3-indirect | `UploadIndirectIndexedArgs` / `ExecuteIndirectIndexed` |
| T-ibl-real | ibl_baker → `ibl_pack.ibl1` |
| T-gpu-headless | D3D12 offscreen + 读回 |
| T-learn-core/electives | learn 阶梯 |
