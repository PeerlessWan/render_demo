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

// True when DXR demo can run on D3D12 with raytracing feature and non-empty config.
bool CanRunDxrDemo(const FeatureSet& features, const DxrDemoConfig& demo);

}  // namespace engine::rt
