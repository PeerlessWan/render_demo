#pragma once

#include "engine/core/feature.h"
#include "engine/core/result.h"
#include "engine/rhi/backend.h"

namespace engine::rt {

struct RaytracingConfig {
  bool enable = false;
  bool allow_fallback = true;
};

enum class RtStatus {
  Disabled,
  Supported,
  UnsupportedFallback,
  Unavailable,
};

RtStatus Resolve(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg);
Status EnsureSafe(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg);

}  // namespace engine::rt
