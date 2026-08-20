#pragma once

#include "engine/core/feature.h"
#include "engine/core/result.h"
#include "engine/rhi/backend.h"
#include "engine/rhi/i_device.h"

#include <filesystem>
#include <vector>

namespace engine::rt {

struct RaytracingConfig {
    bool enable = false;
    bool allow_fallback = true;
};

struct DxrDemoConfig {
    bool enable_reflections = false;
    bool enable_shadows = false;
    int max_bounces = 1;
};

enum class RtStatus {
    Disabled,
    Supported,
    UnsupportedFallback,
    Unavailable,
};

RtStatus Resolve(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg);
Status EnsureSafe(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg);

// Ephemeral D3D12 device probe (OPTIONS5 RaytracingTier). Does not enable a full DXR frame.
[[nodiscard]] bool ProbeDxrHardwareSupport();

// True when DXR demo can run: D3D12 + raytracing feature + non-empty demo config.
// Call ProbeDxrHardwareSupport + SetFeatureOverride("raytracing", ...) before QueryFeatures
// when no live device has published the flag yet (see learn/19_dxr_intro).
bool CanRunDxrDemo(const FeatureSet& features, const DxrDemoConfig& demo);

// W4/W7: when raytracing + shadows gated, records WOULD run and prefers real AS/DispatchRays
// via TryBuildCubeBlasTlasAndDispatchRays when hardware is present (ADR 0030 W7 deepen).
struct DxrShadowDemoResult {
    bool would_run = false;
};

[[nodiscard]] DxrShadowDemoResult DxrShadowDemo(const FeatureSet& features,
                                                                                                const DxrDemoConfig& demo);

// Prefers TryBuildCubeBlasTlasAndDispatchRays; falls back to empty-TLAS prebuild + Ok stub.
// Unavailable when raytracing feature is off. VK path remains Feature/SKIP.
Status RunDxrFullscreenStub(rhi::IDevice& device);

// Tiny DXR-header helper: query empty-TLAS prebuild sizes on a transient device, or
// Unavailable when DXR tier / Device5 is missing (skips with clear Status).
Status TryEmptyTlasPrebuild();

// W7: on DXR hardware, build BLAS (one triangle) + TLAS, create minimal RTPSO from
// dxr_shadow_lib.cso when present, DispatchRays (8x8). If StateObject/lib missing, returns Ok
// after AS build with LogInfo that DispatchRays PSO is missing. VK: Unavailable.
Status TryBuildCubeBlasTlasAndDispatchRays(
        const std::filesystem::path& dxr_lib_dxil = {});

// Mega-W9: compose a small DXR shadow demo result into a float overlay factor
// (not fullscreen production RT). Ok when demo/AS path ran; factor in (0,1].
[[nodiscard]] Status TryComposeDxrShadowOverlay(float& out_shadow_factor);

// Mega-W9/W11: when VK_KHR_ray_tracing_pipeline is present, resolve vkCmdTraceRaysKHR
// (real TraceRays entrypoint). Ok when entrypoint ready; else Unavailable SKIP.
// Does not run a fullscreen production RT frame (no SBT/raygen product path).
[[nodiscard]] Status TryVkTraceRaysDemoStub();

// W17/W20 ADR 0041/0043: half-res soft-shadow blur grid; not fullscreen RT.
// Fills mean factor; optional out_grid (w*h floats in [0,1]) for UploadSoftShadowMask.
[[nodiscard]] Status TryHalfResSoftShadowCompose(float& out_shadow_factor);
[[nodiscard]] Status TryHalfResSoftShadowCompose(float& out_shadow_factor,
                                                 std::vector<float>& out_grid, int& out_w,
                                                 int& out_h);

// W23 / ADR 0046: productize one step — prefer DXR overlay into soft-shadow mask grid.
// Falls back to TryHalfResSoftShadowCompose when DXR unavailable (honest).
[[nodiscard]] Status TryProductSoftShadowMask(std::vector<float>& out_grid, int& out_w, int& out_h);

// W23: half-res RT reflection stand-in — fills RGBA8 buffer (w*h*4) from DXR demo when
// enable_reflections; otherwise Unavailable SKIP (SSR remains the raster product path).
[[nodiscard]] Status TryHalfResRtReflectionCompose(std::vector<std::uint8_t>& out_rgba, int& out_w,
                                                   int& out_h);

// Alias (W16 name).
[[nodiscard]] inline Status TrySoftShadowCompose(float& out_shadow_factor) {
    return TryHalfResSoftShadowCompose(out_shadow_factor);
}

}  // namespace engine::rt
