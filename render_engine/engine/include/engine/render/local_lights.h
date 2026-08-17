#pragma once

#include "engine/core/math.h"
#include "engine/material/material.h"
#include "engine/render/shadow_atlas.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// M26 / C02: CPU list may hold more; FrameCB uploads ≤ kMaxLocalLightsGpu.
inline constexpr int kMaxLocalLightsCpu = 16;
inline constexpr int kMaxLocalLightsGpu = 16;
inline constexpr int kMaxLocalShadowLights = 2;  // Shadow Atlas cubemap/spot slots

// Coarse screen-tile grid for AssignLightsToTiles (Forward+ style CPU lists).
inline constexpr int kLightTileGridW = 8;
inline constexpr int kLightTileGridH = 4;
inline constexpr int kLightTileCount = kLightTileGridW * kLightTileGridH;

struct LocalLight {
  int id = 0;
  Vec3 position{0, 0, 0};
  float range = 8.f;
  ColorRgba color{1, 0.95f, 0.85f, 1.f};
  float intensity = 1.f;
  // Spot: direction is axis; angles are half-angles from axis (deg).
  // spot_angle_deg >= 179 → point/omni (default).
  Vec3 direction{0.f, -1.f, 0.f};
  float spot_angle_deg = 180.f;
  float spot_inner_deg = 160.f;
  // C03/W7: 0=off, 1=narrow, 2=wide, 3=batwing (analytic IES factor in lit shader).
  int ies_profile = 0;
  bool cast_shadows = true;
  int shadow_resolution = 512;  // atlas tile extent
};

[[nodiscard]] inline bool IsSpotLight(const LocalLight& light) {
  return light.spot_angle_deg < 179.f;
}

// Forward+ tile list (CPU): bin light indices into a coarse screen grid by projecting
// each light's world position with view_proj. Not a full GPU clustered-lighting path —
// intended for unit tests and optional Sandbox / debug tooling.
void AssignLightsToTiles(const std::vector<LocalLight>& lights, const Mat4& view_proj,
                         int grid_w, int grid_h,
                         std::vector<std::vector<int>>& out_tiles);

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

// Omnidirectional: 6 face VPs (+X,-X,+Y,-Y,+Z,-Z), 90° FOV, for 2D atlas packing.
// Face index matches lit_cube.hlsl LocalShadowFactor major-axis selection.
std::array<Mat4, 6> BuildLocalShadowCubeMatrices(const LocalLight& light);

}  // namespace engine::render
