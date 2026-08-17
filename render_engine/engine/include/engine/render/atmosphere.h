#pragma once

#include "engine/core/math.h"

namespace engine::render {

// C05 start: thin analytical sky (CPU single-scatter approx). Not volume clouds / weather.
struct AtmosphereParams {
  // Relative Rayleigh RGB scales (higher blue → cooler zenith).
  Vec3 rayleigh_scale{1.0f, 1.35f, 2.2f};
  Vec3 sun_dir{0.3f, -1.f, 0.2f};  // world-space direction toward sun (or from scene).
  float turbidity = 2.f;           // ≥1; higher → hazier / warmer horizon.
};

// Analytical single-scatter sky color for a view direction (world space).
[[nodiscard]] ColorRgba EvalSkyColor(const AtmosphereParams& params, const Vec3& dir);

}  // namespace engine::render
