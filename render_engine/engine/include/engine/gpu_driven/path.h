#pragma once

#include "engine/core/feature.h"
#include "engine/core/result.h"

namespace engine::gpu_driven {

enum class Path {
  RasterFallback,
  IndirectDraw,
  MeshShader,
};

struct GpuDrivenConfig {
  bool enable_indirect = true;
  bool enable_mesh_shader = false;
  bool enable_gpu_cull = true;
};

Path SelectPath(const GpuDrivenConfig& cfg, const FeatureSet& features);
Status ValidateConfig(const GpuDrivenConfig& cfg);

}  // namespace engine::gpu_driven
