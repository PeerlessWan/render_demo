#include "engine/gpu_driven/path.h"

#include "engine/core/feature.h"

namespace engine::gpu_driven {

Path SelectPath(const GpuDrivenConfig& cfg, const FeatureSet& features) {
  (void)features;
  // C08/M26: MeshShader is Feature SKIP — Path enum kept; only select when capability
  // is explicitly reported (SetFeatureOverride("mesh_shader", true) for future devices).
  if (cfg.enable_mesh_shader && QueryFeature("mesh_shader")) {
    return Path::MeshShader;
  }
  if (cfg.enable_indirect) {
    return Path::IndirectDraw;
  }
  return Path::RasterFallback;
}

Status ValidateConfig(const GpuDrivenConfig& cfg) {
  if (!cfg.enable_indirect && cfg.enable_mesh_shader) {
    return Status::Fail(ErrorCode::InvalidArgument,
                        "mesh shader path requires indirect or explicit mesh support flag");
  }
  return Status::Ok();
}

}  // namespace engine::gpu_driven
