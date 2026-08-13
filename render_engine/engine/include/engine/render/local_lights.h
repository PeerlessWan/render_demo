#pragma once

#include "engine/core/math.h"
#include "engine/material/material.h"
#include "engine/render/shadow_atlas.h"

#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

struct LocalLight {
  int id = 0;
  Vec3 position{0, 0, 0};
  float range = 8.f;
  ColorRgba color{1, 0.95f, 0.85f, 1.f};
  float intensity = 1.f;
  bool cast_shadows = true;
  int shadow_resolution = 512;  // atlas tile extent
};

// Packs local-light shadow maps into ShadowAtlas (CPU schedule; GPU cube/2D writes later).
class LocalLightShadowScheduler {
 public:
  void Clear();
  void AddLight(const LocalLight& light);
  bool Pack(ShadowAtlas& atlas);

  [[nodiscard]] const std::vector<LocalLight>& lights() const { return lights_; }
  [[nodiscard]] const std::vector<ShadowAtlasSlot>& slots() const { return slots_; }
  [[nodiscard]] int packed_count() const { return static_cast<int>(slots_.size()); }

 private:
  std::vector<LocalLight> lights_;
  std::vector<ShadowAtlasSlot> slots_;
};

material::PbrMaterial ResolveMeshMaterial(std::string_view mesh_id);

// Perspective VP for first local light (spot-like shadow from light toward target).
Mat4 BuildLocalShadowMatrix(const LocalLight& light, const Vec3& look_at = Vec3{0, 0, 0});

}  // namespace engine::render
