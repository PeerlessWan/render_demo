#pragma once

#include <cmath>
#include <string>
#include <string_view>

namespace engine::render {

// C03 / Mega-W9: minimal Light Function factor (0..1) for local lights.
enum class LightFunctionProfile {
  Off = 0,
  SoftDisk = 1,      // radial soft disk from UV radius in [0,1]
  RadialFalloff = 2, // 1 - t^2 style
  AngleCos = 3,      // cos-gated angle factor (uv_or_angle as cosθ in [-1,1] or [0,1])
};

[[nodiscard]] LightFunctionProfile ParseLightFunctionId(std::string_view id);

// Evaluate modulation factor in [0,1]. uv_or_angle meaning depends on profile:
// SoftDisk / RadialFalloff: distance from center in [0,1] (clamped);
// AngleCos: cos(theta) in [-1,1] (mapped).
[[nodiscard]] float EvalLightFunctionFactor(LightFunctionProfile profile, float uv_or_angle);

}  // namespace engine::render
