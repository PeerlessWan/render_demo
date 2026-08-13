#include "engine/gpu_driven/path.h"

namespace engine::gpu_driven {

Path SelectPath(const GpuDrivenConfig& cfg, const FeatureSet& features) {
  if (cfg.enable_mesh_shader && features.level == FeatureLevel::L2) {
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
