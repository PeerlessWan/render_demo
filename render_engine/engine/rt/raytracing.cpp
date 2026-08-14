#include "engine/rt/raytracing.h"

namespace engine::rt {

RtStatus Resolve(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg) {
  if (!cfg.enable) {
    return RtStatus::Disabled;
  }
  if (backend == rhi::Backend::D3D12 && features.raytracing) {
    return RtStatus::Supported;
  }
  if (backend == rhi::Backend::Vulkan && features.raytracing) {
    return RtStatus::Supported;
  }
  if (cfg.allow_fallback) {
    return RtStatus::UnsupportedFallback;
  }
  return RtStatus::Unavailable;
}

Status EnsureSafe(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg) {
  const auto st = Resolve(backend, features, cfg);
  if (st == RtStatus::Unavailable) {
    return Status::Fail(ErrorCode::Unavailable, "raytracing requested but unsupported");
  }
  return Status::Ok();
}

bool CanRunDxrDemo(const FeatureSet& features, const DxrDemoConfig& demo) {
  if (!features.raytracing || !features.d3d12) {
    return false;
  }
  return demo.enable_reflections || demo.enable_shadows;
}

}  // namespace engine::rt
