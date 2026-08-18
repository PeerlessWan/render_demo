#pragma once

#include "engine/core/math.h"
#include "engine/material/material.h"
#include "engine/render/shadow_atlas.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// M26 / Mega-W10 C02: CPU list may hold more; FrameCB uploads ≤ kMaxLocalLightsGpu.
inline constexpr int kMaxLocalLightsCpu = 32;
inline constexpr int kMaxLocalLightsGpu = 32;
inline constexpr int kMaxLocalShadowLights = 2;  // Shadow Atlas cubemap/spot slots

// Coarse screen-tile grid for AssignLightsToTiles (Forward+ style CPU lists).
inline constexpr int kLightTileGridW = 8;
inline constexpr int kLightTileGridH = 4;
inline constexpr int kLightTileCount = kLightTileGridW * kLightTileGridH;
// Mega-W10 C02: coarse view-space Z slices (cluster = slice * tile_count + tile).
inline constexpr int kLightZSlices = 4;
inline constexpr int kLightClusterCount = kLightTileCount * kLightZSlices;
inline constexpr float kLightZNear = 0.5f;
inline constexpr float kLightZFar = 80.f;
// Packed into FrameCB / lit PS (max lights accumulated per cluster).
inline constexpr int kMaxLightsPerTile = 8;
inline constexpr int kTileLightIndexCount = kLightClusterCount * kMaxLightsPerTile;

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
  // Mega-W9 C03: optional Light Function id ("soft_disk" / "radial" / "angle_cos" / empty=off).
  std::string light_function_id;
  bool cast_shadows = true;
  int shadow_resolution = 512;  // atlas tile extent
};

[[nodiscard]] inline bool IsSpotLight(const LocalLight& light) {
  return light.spot_angle_deg < 179.f;
}

// Map view-space depth (along cam_forward) into [0, kLightZSlices).
[[nodiscard]] inline int ViewZToSlice(float view_z, float z_near = kLightZNear,
                                      float z_far = kLightZFar) {
  float denom = z_far - z_near;
  if (denom < 1e-5f) {
    denom = 1e-5f;
  }
  float t = (view_z - z_near) / denom;
  if (t < 0.f) {
    t = 0.f;
  }
  if (t > 0.999f) {
    t = 0.999f;
  }
  const int slice = static_cast<int>(t * static_cast<float>(kLightZSlices));
  return slice < kLightZSlices ? slice : (kLightZSlices - 1);
}

// Forward+ tile+Z list (CPU reference matching light_tile_cull_cs): bin each light into
// all screen tiles overlapped by its projected range sphere, and all coarse Z slices
// overlapped by center±range along cam_forward.
// out_tiles size = grid_w * grid_h * kLightZSlices; index = slice * (gw*gh) + ty*gw + tx.
void AssignLightsToTiles(const std::vector<LocalLight>& lights, const Mat4& view_proj,
                         int grid_w, int grid_h,
                         std::vector<std::vector<int>>& out_tiles,
                         const Vec3& eye = Vec3{0.f, 0.f, 0.f},
                         const Vec3& cam_forward = Vec3{0.f, 0.f, -1.f},
                         float z_near = kLightZNear, float z_far = kLightZFar);

// Alias: AssignLightsToTiles IS the CPU reference for the tile CS contract.
inline void CullLightsToTilesCpuReference(const std::vector<LocalLight>& lights,
                                          const Mat4& view_proj, int grid_w, int grid_h,
                                          std::vector<std::vector<int>>& out_tiles,
                                          const Vec3& eye = Vec3{0.f, 0.f, 0.f},
                                          const Vec3& cam_forward = Vec3{0.f, 0.f, -1.f},
                                          float z_near = kLightZNear, float z_far = kLightZFar) {
  AssignLightsToTiles(lights, view_proj, grid_w, grid_h, out_tiles, eye, cam_forward, z_near,
                      z_far);
}

// Flatten AssignLightsToTiles output into fixed FrameCB arrays (≤kMaxLightsPerTile each).
void PackTileLightLists(const std::vector<std::vector<int>>& tiles,
                        std::array<int, kLightClusterCount>& out_counts,
                        std::array<int, kTileLightIndexCount>& out_indices);

// CPU stand-in for light_tile_cull_cs: same range-bin + Z-slice + pack as AssignLightsToTiles /
// PackTileLightLists. Device Dispatch may call this when GPU UAV path is stubbed.
void SimulateLightTileCullCs(const Mat4& view_proj, std::span<const Vec3> positions,
                             std::span<const float> ranges,
                             std::array<int, kLightClusterCount>& out_counts,
                             std::array<int, kTileLightIndexCount>& out_indices,
                             const Vec3& eye = Vec3{0.f, 0.f, 0.f},
                             const Vec3& cam_forward = Vec3{0.f, 0.f, -1.f},
                             float z_near = kLightZNear, float z_far = kLightZFar);

// CPU eval path matching lit PS cluster pick from screen UV + view-space Z.
void EvalTiledLightList(const std::array<int, kLightClusterCount>& counts,
                        const std::array<int, kTileLightIndexCount>& indices, float u, float v,
                        float view_z, std::vector<int>& out_lights,
                        float z_near = kLightZNear, float z_far = kLightZFar);

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
