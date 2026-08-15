#pragma once

#include "engine/core/feature.h"
#include "engine/core/result.h"

namespace engine::gpu_driven {

// M24/M26 geometry submission path.
// MeshShader: enum retained for API stability; Feature SKIP this wave (C08 / ADR 0032)
// until QueryFeature("mesh_shader") is reported by a live device. Hosts must not assume
// mesh-shader PSOs exist when SelectPath returns anything other than MeshShader under
// an explicit override.
enum class Path {
  RasterFallback,
  IndirectDraw,
  MeshShader,  // Feature SKIP unless override "mesh_shader"=true
};

struct GpuDrivenConfig {
  bool enable_indirect = true;
  bool enable_mesh_shader = false;  // advisory; skipped unless mesh_shader feature is on
  bool enable_gpu_cull = true;
};

Path SelectPath(const GpuDrivenConfig& cfg, const FeatureSet& features);
Status ValidateConfig(const GpuDrivenConfig& cfg);

}  // namespace engine::gpu_driven
