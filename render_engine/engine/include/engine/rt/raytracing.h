#pragma once

#include "engine/core/feature.h"
#include "engine/core/result.h"
#include "engine/rhi/backend.h"

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

}  // namespace engine::rt
