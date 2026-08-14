# Windows Vulkan ↔ D3D12 capability matrix (Wave5+ / W-vk-parity)

> Linux (M18) 仍外置。本文件跟踪 **Windows** 上 Vulkan 对标 D3D12 的状态。

| 能力 | D3D12 | Vulkan (Win) | 备注 |
|---|---|---|---|
| 清屏 / Swapchain | 有 | 有 | |
| Lit + 深度 | 有 | 有 | |
| CSM 阴影 | 有 | 有 | |
| 自定义 mesh slot + albedo/ORM | 有 | **有** | `UploadLitGeometry` / `UploadLitAlbedoRgba` / ORM；slot 0–7 |
| 点光 + 局部影 | 有 | **有** | FrameCB local lights；atlas LOAD + lit compare |
| HDR scene + Post | 有 | **有**（`scene_color` → tonemap） | SSAO/TAA 开关接入；全栈可扩 |
| Skybox | 有 | **有** | `skybox_vk` SPIR-V；Setup/Upload/DrawSkybox |
| IBL irradiance + prefilter + BRDF LUT | 有 | **有** | bindings 3 / 8 / 9 |
| UI / Debug / ScreenQuad | 有 | **有** | `ui_imgui_vk` / `debug_line_vk` / `quad_vk` |
| 实例化 / Indirect | GPU 实例化 + ExecuteIndirect | **Feature + CPU 回退** | Sandbox scale 路径已开 |
| 视频硬解 | D3D12VA stub→探测 | Vulkan Video SKIP | 无扩展不假成功 |
| RT 示范 | DXR Feature | VK_KHR_ray_tracing SKIP | |
| gpu_headless 读回 | 有 | **真 GPU Readback** | 需 BeginFrame 内调用 |

验收：`sample_sandbox --backend=vulkan` 观感对齐 D3D12（砖地/头盔/天空/点光/IBL/ImGui/网格）；C4 见 `run_backend_parity.py`（默认 ROI；`--strict` / CI `-StrictParity` 可选严）。
