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
  // When true, albedo slot is expected to carry lightmap.rgba (baker format) or a
  // CPU-multiplied albedo×lightmap upload via gi::LoadLightmapRgba + UploadLitAlbedoRgba.
  bool use_lightmap = false;
  // W20 VT opt-in: with Feature vt_near_default/virtual_texture, sample secondary albedo
  // (UploadLitAlbedoRgba slot=1 physical atlas). Default false → classic path zero-diff.
  bool use_virtual_texture = false;
  bool transparent = false;
  int mesh_slot = 0;
  int tex_slot = 0;
  float uv_scale = 1.f;
  // W21 Godot StandardMaterial3D subset (ADR 0044)
  ColorRgba emission{0.f, 0.f, 0.f, 1.f};
  float emission_energy = 0.f;
  enum class CullMode : std::uint8_t { Back = 0, Front = 1, None = 2 };
  enum class TransparencyMode : std::uint8_t { Opaque = 0, Alpha = 1, AlphaScissor = 2 };
  CullMode cull = CullMode::Back;
  TransparencyMode transparency_mode = TransparencyMode::Opaque;
  float alpha_scissor = 0.5f;
};

// Bake emission into base color (lit path has no separate emission channel yet).
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

}  // namespace engine::material
