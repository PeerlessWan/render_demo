# Windows Vulkan ↔ D3D12 capability matrix (Wave5+ / four-track 100%)

> Linux (M18) 仍外置。本文件跟踪 **Windows** 上 Vulkan 对标 D3D12 的状态。

| 能力 | D3D12 | Vulkan (Win) | 备注 |
|---|---|---|---|
| 清屏 / Swapchain | 有 | 有 | |
| Lit + 深度 | 有 | 有 | |
| CSM 阴影 | 有 | 有 | |
| 点光 6-face 阴影 | 有 | **有（atlas LOAD + Feature）** | 与 CSM 共用 atlas；采样可继续加深 |
| HDR scene + Post | 有 | **有（SPIR-V tonemap 全屏乘）** | SSAO/TAA 开关接入 push；全栈可扩 |
| IBL / 反射探针上传 | 有 | **真 cubemap 上传 + irradiance 采样** | BRDF LUT 接受；prefilter 可绑同一 cube |
| UI / Debug lines | 有 | 部分 | |
| 实例化 / Indirect | GPU 实例化 + ExecuteIndirect | **Feature + CPU 回退** | `UploadInstanceTransforms` / `UploadIndirectIndexedArgs` |
| 视频硬解 | D3D12VA stub→探测 | Vulkan Video SKIP | 无扩展不假成功 |
| RT 示范 | DXR Feature | VK_KHR_ray_tracing SKIP | |
| gpu_headless 读回 | 有 | headless 走 CPU stub | |

验收：`sample_sandbox --backend=vulkan` 主路径可跑（IBL+post+局部影 Feature）；缺能力须 `QueryFeature`/`Status` 可诊断。
