# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空）D3D12 产品波次 + Learn + Vulkan 对标骨架已收口 |  |  |

---

## Todo（下一档）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P2 | T-rmlui-real | 真 RmlUi | **阻塞**：需 vendor |
| P2 | T-http-tls | OpenSSL HTTPS | **阻塞**：缺 SDK |
| P3 | T-linux-m18 | Linux + Vulkan | 外置（见 LINUX_VULKAN.md） |
| P3 | T-ground-slab | 悬浮浅色层切物体 | **搁置** |
| P3 | T-vk-post-full | Vulkan 完整 post/点光影采样 | 矩阵见 [VULKAN_PARITY.md](VULKAN_PARITY.md) |
| P3 | T-m14-bindless-full | 全 Bindless 迁移 | 非本口径必须 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `fb5c53a`（GPU headless / TAA / 探针门控） |
| 本档回退 | 还原 IBL pack / scene_capture / learn samples / wave stubs |

验证：`scripts/ci_headless.ps1`；`content/ibl/ibl_pack.ibl1`；Sandbox `--gpu-headless`；`--backend=vulkan` 冒烟

---

## Done（近期）

| 项 | 说明 |
|---|---|
| T-ibl-real | ibl_baker → `ibl_pack.ibl1` + lit 分裂和采样 |
| T-probe-capture | `CaptureApproximateSceneFaces` 动态反射近似 |
| T-lightmap-baker | `tools/lightmap_baker` |
| T-learn-core | learn 必修 03–11 |
| T-learn-electives | 12/15/16/18/19/22/24/25/26/27/29/30/32 |
| T-wave2-4-stubs | 实例化/遮挡/indirect/upscaler/ProbeVolume/水面/图集 |
| T-vk-parity-win | Windows Vulkan 对标骨架 + [VULKAN_PARITY.md](VULKAN_PARITY.md) |
| T-gpu-headless | D3D12 offscreen + 读回断言 |
| T-taa-mv | TAA + 深度重投影 |
| T-m13-dynref | ReflectionProbe Cubemap |
| T-m14-gates | Feature / SubmitConfig / HDR10 尝试 |
