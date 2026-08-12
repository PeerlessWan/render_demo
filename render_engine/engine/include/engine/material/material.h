#pragma once

#include "engine/core/math.h"

#include <string>

namespace engine::material {

struct PbrMaterial {
  ColorRgba base_color{1, 1, 1, 1};
  float metallic = 0.f;
  float roughness = 0.5f;
  std::string albedo_tex;
  std::string normal_tex;
  std::string orm_tex;
  bool clearcoat = false;
};

}  // namespace engine::material
