# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空）本档 deepen/test/MCP 已收口 |  |  |

---

## Todo（下一档 / 阻塞）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P2 | T-rmlui-real | 真 RmlUi | **阻塞**：需 vendor |
| P2 | T-http-tls | OpenSSL HTTPS | **阻塞**：缺 SDK |
| P3 | T-linux-m18 | Linux + Vulkan | 外置（见 LINUX_VULKAN.md） |
| P3 | T-ground-slab | 悬浮浅色层切物体 | **搁置** |
| P3 | T-m14-bindless-full | 全 Bindless 迁移 | 非本口径必须 |
| P3 | T-hiz-full | HiZ 真遮挡 | 未做 |
| P3 | T-vk-post-spirv-full | Vulkan 全 SPIR-V post 栈 | exposure 已接；全栈可扩 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `0354878`（产品波次） |
| 本档回退 | 还原实例化/GPU 探针/Indirect/VK IBL/golden/harness/MCP |

验证：`scripts/ci_headless.ps1`；unit 76+；Sandbox `--gpu-headless`；`--harness-stdio`；`sandbox_mcp`

---

## Done（近期）

| 项 | 说明 |
|---|---|
| W-a-test | TESTING/看板/缺口对齐；`--backend=d3d12`；黄金图脚本；`ENGINE_GOLDEN_DUMP` |
| W-b1-inst | D3D12 `DrawLitInstanced` + learn22 |
| W-b2-probe | `CaptureReflectionProbeGpu` + CPU fallback |
| W-b3-indirect | `UploadIndirectIndexedArgs` / `ExecuteIndirectIndexed` |
| W-c-vk | Vulkan IBL cubemap 上传采样；local shadow Feature skip；post exposure |
| W-d-mcp | Harness JSON + `sandbox_mcp` + [SANDBOX_MCP.md](SANDBOX_MCP.md) |
| T-ibl-real | ibl_baker → `ibl_pack.ibl1` |
| T-gpu-headless | D3D12 offscreen + 读回 |
| T-learn-core/electives | learn 阶梯 |
