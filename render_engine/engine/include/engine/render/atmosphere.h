#pragma once

#include "engine/core/math.h"

namespace engine::render {

// C05 / W7: thin analytical sky + cloud-band (not full weather / volume-cloud product).
struct AtmosphereParams {
  // Relative Rayleigh RGB scales (higher blue → cooler zenith).
  Vec3 rayleigh_scale{1.0f, 1.35f, 2.2f};
  Vec3 sun_dir{0.3f, -1.f, 0.2f};  // world-space direction toward sun (or from scene).
  float turbidity = 2.f;           // ≥1; higher → hazier / warmer horizon.
  // Cloud band: soft noise sheet around elevation band (0 = off contribution).
  float cloud_coverage = 0.45f;
  float cloud_elevation = 0.22f;  // preferred |dir.y| center
  float cloud_thickness = 0.18f;
};

// Analytical single-scatter sky color for a view direction (world space).
[[nodiscard]] ColorRgba EvalSkyColor(const AtmosphereParams& params, const Vec3& dir);

// Soft cloud opacity in [0,1] for a view direction (CPU raymarch-ish band).
[[nodiscard]] float EvalCloudBand(const AtmosphereParams& params, const Vec3& dir);

// Sky + cloud composite (clouds tint toward white/grey).
[[nodiscard]] ColorRgba EvalSkyWithClouds(const AtmosphereParams& params, const Vec3& dir);

// Couple volumetric fog defaults with atmosphere tint (host / Sandbox).
struct CoupledFog {
  ColorRgba fog_color{0.62f, 0.70f, 0.78f, 1.f};
  float fog_density = 0.02f;
  ColorRgba clear_color{0.10f, 0.12f, 0.16f, 1.f};
};
[[nodiscard]] CoupledFog CoupleFogWithAtmosphere(const AtmosphereParams& params,
                                                 const Vec3& view_dir, float base_fog_density,
                                                 bool enable_clouds);

}  // namespace engine::render
