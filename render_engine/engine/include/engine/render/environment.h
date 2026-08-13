#pragma once

#include "engine/core/math.h"

#include <string>

namespace engine::render {

struct Environment {
  ColorRgba ambient{0.16f, 0.17f, 0.20f, 1.f};
  Vec3 sun_direction{0.3f, -1.f, 0.2f};
  ColorRgba sun_color{1.f, 0.96f, 0.9f, 1.f};
  float sun_intensity = 4.2f;
  std::string ibl_irradiance;
  std::string ibl_prefilter;
  std::string ibl_brdf_lut;

  [[nodiscard]] bool has_ibl() const { return !ibl_irradiance.empty(); }
};

}  // namespace engine::render
