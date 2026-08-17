#pragma once

#include "engine/core/math.h"

#include <string>

namespace engine::render {

// Environment hub (ARCHITECTURE §4.6): skybox / IBL / fog / clear / exposure defaults.
struct Environment {
  ColorRgba ambient{0.16f, 0.17f, 0.20f, 1.f};
  ColorRgba clear_color{0.10f, 0.12f, 0.16f, 1.f};
  Vec3 sun_direction{0.3f, -1.f, 0.2f};
  ColorRgba sun_color{1.f, 0.96f, 0.9f, 1.f};
  float sun_intensity = 4.2f;
  float exposure = 1.2f;

  // IBL pack paths (often the same ibl_pack.ibl1).
  std::string ibl_irradiance;
  std::string ibl_prefilter;
  std::string ibl_brdf_lut;
  // SKY1 cubemap path for background display (independent of IBL lighting).
  std::string skybox_cubemap;

  // Fog defaults (copied into EffectTuning once at startup).
  bool fog_enabled = false;
  float fog_density = 0.02f;
  float fog_start = 12.f;
  ColorRgba fog_color{0.62f, 0.70f, 0.78f, 1.f};

  bool skybox_enabled = true;
  // C05: when true, Sandbox/host may tint fog_color / clear from EvalSkyColor(cam forward).
  bool enable_atmosphere = false;
  // W7: soft cloud band on top of atmosphere tint (requires enable_atmosphere for CoupleFog path).
  bool enable_volume_clouds = false;

  [[nodiscard]] bool has_ibl() const { return !ibl_irradiance.empty(); }
  [[nodiscard]] bool has_skybox() const { return !skybox_cubemap.empty(); }
};

}  // namespace engine::render
