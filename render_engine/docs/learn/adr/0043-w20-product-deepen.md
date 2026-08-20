# ADR 0043: W20 中台产品级加深

- 状态: Accepted
- 日期: 2026-08-20
- 关联: ADR 0042、KNOWN_GAPS §3–4、VULKAN_PARITY、STANDARDS §15

## 背景

W18/W19 半落地收口后，仍有四类工程弱项：深度不均、双后端成本、device monolith、缺性能/预算产品指标；GI/RT/VT·HLOD·MS 多为 API+示范，未进「内容管线可感的产品 Pass」。

## 决策

1. **水位**：中台产品级 + 真 GPU 路径 + 工程可维护/可度量；**不宣称** Lumen / RTXGI / Nanite / 默认全材质 VT。
2. **本波做**：
   - **A** GI atlas→GPU+lit 空间采样；软影 half-res 进 FrameCB；VT opt-in；HLOD bake 上纹理；MS 主设备热路径
   - **B** WorldText 纹理路径；热更 hlsl→cso→PSO；HLOD/软影产品 Pass
   - **C** W20 新路径标 L0 则双端同波；更新 `VULKAN_PARITY.md`
   - **D** `d3d12`/`vulkan_device` 按域拆分 + `GpuComputeOneShot`；次级拆 glTF/particles/rt（**Done**）
   - **E** Profiler Pass GPU 时间 + StreamingBudget HUD + 规模冒烟契约
3. **冻结（本波及后续暂不开发）**：DLSS / FSR2 / MsQuic 真 SDK；超分继续 `builtin_bilinear`；QUIC 继续 Probe/Unavailable。
4. **不做**：真 NVIDIA DDGI、Nanite、多视角商业 Impostor、引擎内复制、backend 拆独立进程、mac/C17。

## W20 能力表（L0/L1/L2）

| 能力 | 层级 | D3D12 | Vulkan | 备注 |
|---|---|---|---|---|
| GI atlas 上 GPU + lit 采样 | L0 | 同波 | 同波 | DDGI-lite；非 RTXGI |
| 软影 half-res → FrameCB | L0 | 同波 | 同波 | 无 RT → SKIP |
| VT 物理页 + lit opt-in | L0 | 同波 | 同波 | 默认 albedo 零差 |
| HLOD bake → UploadLitAlbedo | L0 | 同波 | 同波 | 非多视角 |
| Mesh Shader 主设备路径 | L1 | Tier→Ok | EXT→Ok | 无能力 SKIP |
| WorldText 纹理 + lit | L0 | 同波 | 同波 | DebugDraw 仅调试 |
| 热更 cso→RebuildLitPso | L1 | dxc | dxc/spirv 尽力 | 缺工具 SKIP |
| GpuComputeOneShot + tile cull | L0 | 同波 | 同波 | 新 CS 必走 helper |
| Profiler GPU Pass + Budget HUD | L0 | 同波 | 同波 | 无硬件勿假造 ms |

## 拆分清单（D1）

对称落盘（目标主文件 ≤~2000 行）：

- `*_device_core.cpp` — Init/Present/swapchain/fence/timestamps
- `*_device_lit.cpp` — lit/upload/draw/shadow/sky
- `*_device_post.cpp` — post / UI / debug / quads
- `*_device_compute.cpp` — instance cull、light tile、VT CS
- `*_device_resources.cpp` — buffer/texture/descriptor helpers
- `gpu_compute_oneshot_{d3d12,vk}.*` — one-shot CS + readback

纯拆分与功能加深分提交语义；不改对外 `IDevice` 语义。

## 后果

- 优点：主帧可感、双端诚实、monolith 可维护、有预算/GPU 时间产品指标
- 代价：FrameCB/lit 双端同步成本；拆分易冲突（每块立即测）

## 收口备注（2026-08-20）

- Device：`d3d12`/`vulkan_device` 按域拆分 + `GpuComputeOneShot`；tile cull 走 helper；次级拆 glTF/particles/rt **Done**。
- GI：质量档 `probe_update_budget`；Sandbox local lights + CascadeRefine(focus)；atlas→GPU t11 + lit 采样。
- 软影：half-res grid→`UploadSoftShadowMask` t12；不再以乘 `sun_intensity` 为主产品路径。
- VT/HLOD/MS/WorldText/热更：产品 Pass 加深；MS `TryMeshShaderHotPath`；缺能力 SKIP。
- Profiler GPU Pass + StreamingBudget HUD；`test_w20` 规模/预算契约。
- 尾巴：Vulkan `UploadRgba2D` / `EndOneShot` / `UploadLitGeometry`（换槽）与 cull/indirect 扩容改为 **fence 等待**（`WaitGpuSubmitted`），去掉热路径 `Device/QueueWaitIdle`。
- 单测：以看板跑测水位为准。
- **冻结**：DLSS/FSR2/MsQuic。
