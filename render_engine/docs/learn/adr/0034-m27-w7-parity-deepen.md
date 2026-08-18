# ADR 0034: M27/W7 对标加深边界

- 状态: Accepted
- 日期: 2026-08-17
- 关联: PLAN M27+ / W7、KNOWN_GAPS C03–C05/C12/C14、ADR 0030

## 背景

W4–W6 收口后继续对标主流引擎短板，但明确不追 VT/HLOD/IK/Path2D/MsQuic，且不动 editor/game_kit。

## 决策

1. **C05**：`EvalCloudBand` / `CoupleFogWithAtmosphere`；Sandbox F1 `enable_atmosphere` + `enable_volume_clouds`。
2. **C03**：`EvalIesFactor` / `SampleIesLut` + lit `g_local_ies`；非完整 IES 文件生态。
3. **C14**：`BuildWorldTextBillboards`（BMFont 广告牌）；Sandbox DebugDraw 线框示意。
4. **C04**：PostStack `chromatic_aberration`（默认 0）。
5. **C12**：D3D12 `skin_cs` + `TryDispatchGpuSkinD3d12`；VK 本波 SKIP（**W8+ 已有 `gpu_skin_vk`；见 ADR 0035/0036，本条部分 superseded**）。
6. **DXR**：`TryBuildCubeBlasTlasAndDispatchRays` 真 BLAS/TLAS + 8×8 DispatchRays；无硬件 Unavailable。

## 不做

- C06 VT、C07 HLOD、C11 IK、G13 Path2D、MsQuic、完整天气、Mesh Shader 产品 PSO、editor/game_kit。

## 后果

- 单测 `tests/unit/test_m28.cpp`；看板 W7 收口。
