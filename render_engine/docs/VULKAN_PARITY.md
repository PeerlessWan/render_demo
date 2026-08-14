# Windows Vulkan ↔ D3D12 capability matrix (Wave5)

> Linux (M18) 仍外置。本文件跟踪 **Windows** 上 Vulkan 对标 D3D12 的状态。

| 能力 | D3D12 | Vulkan (Win) | 备注 |
|---|---|---|---|
| 清屏 / Swapchain | 有 | 有 | |
| Lit + 深度 | 有 | 有 | |
| CSM 阴影 | 有 | 有 | |
| 点光 6-face 阴影 | 有 | 加深中 / Feature | 无则明确 Status |
| HDR scene + Post | 有 | 加深中 | SPIR-V post |
| IBL / 反射探针上传 | 有 | Upload* 可调；采样对齐中 | |
| UI / Debug lines | 有 | 部分 | |
| 实例化 / Indirect | API+CPU 骨架 | Feature 门控 | |
| 视频硬解 | D3D12VA stub→探测 | Vulkan Video SKIP | 无扩展不假成功 |
| RT 示范 | DXR Feature | VK_KHR_ray_tracing SKIP | |
| gpu_headless 读回 | 有 | headless 走 CPU stub | |

验收：`sample_sandbox --backend=vulkan` 主路径可跑；缺能力须 `QueryFeature`/`Status` 可诊断。
