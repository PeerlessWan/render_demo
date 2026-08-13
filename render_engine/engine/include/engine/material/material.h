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
  bool transparent = false;
  int mesh_slot = 0;
  int tex_slot = 0;
  float uv_scale = 1.f;
};

}  // namespace engine::material
