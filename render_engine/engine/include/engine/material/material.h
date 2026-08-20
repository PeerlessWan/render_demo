#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <string>

namespace engine::material {

struct PbrMaterial {
  ColorRgba base_color{1, 1, 1, 1};
  float metallic = 0.f;
  float roughness = 0.5f;
  std::string albedo_tex;
  std::string normal_tex;
  std::string orm_tex;
  bool use_lightmap = false;
  bool use_virtual_texture = false;
  bool transparent = false;
  int mesh_slot = 0;
  int tex_slot = 0;
  float uv_scale = 1.f;
  // W21 Godot StandardMaterial3D subset
  ColorRgba emission{0.f, 0.f, 0.f, 1.f};
  float emission_energy = 0.f;
  enum class CullMode : std::uint8_t { Back = 0, Front = 1, None = 2 };
  enum class TransparencyMode : std::uint8_t { Opaque = 0, Alpha = 1, AlphaScissor = 2 };
  enum class BlendMode : std::uint8_t { Mix = 0, Add = 1, Mul = 2 };
  CullMode cull = CullMode::Back;
  TransparencyMode transparency_mode = TransparencyMode::Opaque;
  BlendMode blend_mode = BlendMode::Mix;
  float alpha_scissor = 0.5f;
  // W22: detail / triplanar (CPU Effective* helpers; GPU opt-in later)
  std::string detail_albedo_tex;
  float detail_uv_scale = 4.f;
  float detail_blend = 0.f;  // [0,1]
  bool triplanar = false;
  float triplanar_sharpness = 4.f;
};

[[nodiscard]] inline ColorRgba EffectiveBaseColor(const PbrMaterial& m) {
  ColorRgba c = m.base_color;
  if (m.emission_energy > 1e-4f) {
    c.r += m.emission.r * m.emission_energy;
    c.g += m.emission.g * m.emission_energy;
    c.b += m.emission.b * m.emission_energy;
  }
  return c;
}

[[nodiscard]] inline bool WantsAlphaBlend(const PbrMaterial& m) {
  return m.transparent || m.transparency_mode != PbrMaterial::TransparencyMode::Opaque;
}

[[nodiscard]] inline float EffectiveUvScale(const PbrMaterial& m) {
  return m.uv_scale > 0.f ? m.uv_scale : 1.f;
}

}  // namespace engine::material
