#pragma once

#include "engine/core/feature.h"
#include "engine/core/result.h"
#include "engine/rhi/backend.h"
#include "engine/rhi/i_device.h"

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

// W4: when raytracing is true and shadows are gated on, records that a DXR shadow demo
// pass WOULD run. Does not emit DispatchRays (ADR 0030 stub dispatch contract).
struct DxrShadowDemoResult {
  bool would_run = false;
};

[[nodiscard]] DxrShadowDemoResult DxrShadowDemo(const FeatureSet& features,
                                                const DxrDemoConfig& demo);

// Stub fullscreen DXR dispatch contract bound to an IDevice. Returns Ok if a demo pass
// would run; Unavailable when raytracing is off. No DispatchRays / SBT.
Status RunDxrFullscreenStub(rhi::IDevice& device);

// Tiny DXR-header helper: query empty-TLAS prebuild sizes on a transient device, or
// Unavailable when DXR tier / Device5 is missing (skips with clear Status).
Status TryEmptyTlasPrebuild();

}  // namespace engine::rt
