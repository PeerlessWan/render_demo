#include "engine/render/local_lights.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::render {

void AssignLightsToTiles(const std::vector<LocalLight>& lights, const Mat4& view_proj,
                         int grid_w, int grid_h,
                         std::vector<std::vector<int>>& out_tiles) {
  const int gw = std::max(grid_w, 1);
  const int gh = std::max(grid_h, 1);
  const int tile_count = gw * gh;
  out_tiles.assign(static_cast<std::size_t>(tile_count), {});
  for (std::size_t i = 0; i < lights.size(); ++i) {
    const Vec3 ndc = view_proj.TransformPoint(lights[i].position);
    // Behind / degenerate: skip (not expanded by range — simple center bin).
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z)) {
      continue;
    }
    // NDC xy in [-1,1] → tile; clamp so near-edge lights still land in a cell.
    const float u = std::clamp(ndc.x * 0.5f + 0.5f, 0.f, 0.999f);
    const float v = std::clamp(ndc.y * 0.5f + 0.5f, 0.f, 0.999f);
    const int tx = std::min(static_cast<int>(u * static_cast<float>(gw)), gw - 1);
    const int ty = std::min(static_cast<int>(v * static_cast<float>(gh)), gh - 1);
    out_tiles[static_cast<std::size_t>(ty * gw + tx)].push_back(static_cast<int>(i));
  }
}

void LocalLightShadowScheduler::Clear() {
  lights_.clear();
  slots_.clear();
}

void LocalLightShadowScheduler::AddLight(const LocalLight& light) {
  lights_.push_back(light);
}

bool LocalLightShadowScheduler::Pack(ShadowAtlas& atlas) {
  slots_.clear();
  // Larger tiles first for tighter packing.
  std::vector<LocalLight> ordered = lights_;
  std::sort(ordered.begin(), ordered.end(), [](const LocalLight& a, const LocalLight& b) {
    return a.shadow_resolution > b.shadow_resolution;
  });

  bool all_ok = true;
  for (const auto& light : ordered) {
    if (!light.cast_shadows) {
      continue;
    }
    ShadowAtlasSlot slot;
    if (!atlas.Allocate(light.id, light.shadow_resolution, slot)) {
      all_ok = false;
      continue;
    }
    slots_.push_back(slot);
  }
  return all_ok;
}

material::PbrMaterial ResolveMeshMaterial(std::string_view mesh_id) {
  material::PbrMaterial m;
  if (mesh_id == "ground") {
    m.base_color = {0.72f, 0.70f, 0.66f, 1.f};
    m.roughness = 0.92f;
    m.metallic = 0.f;
    m.albedo_tex = "textures/ph/brick_diff.jpg";
    m.orm_tex = "textures/ph/brick_arm.jpg";
    m.uv_scale = 4.f;
    m.mesh_slot = 4;  // subdivided ground plane (not scaled cube)
    m.tex_slot = 0;
  } else if (mesh_id == "metal") {
    m.base_color = {0.86f, 0.87f, 0.9f, 1.f};
    m.roughness = 0.18f;
    m.metallic = 0.95f;
    m.uv_scale = 1.f;
  } else if (mesh_id == "glass") {
    // Opaque tinted “glass”: alpha-blend + no depth write produced a floating white
    // slab on D3D12 that appeared/disappeared with the Transparent profiler pass.
    m.base_color = {0.55f, 0.72f, 0.88f, 1.f};
    m.roughness = 0.12f;
    m.metallic = 0.05f;
    m.transparent = false;
    m.uv_scale = 1.f;
  } else if (mesh_id == "helmet") {
    m.base_color = {1.f, 1.f, 1.f, 1.f};
    m.roughness = 0.45f;
    m.metallic = 0.2f;
    m.albedo_tex = "models/DamagedHelmet";
    m.orm_tex = "models/DamagedHelmet";
    m.mesh_slot = 1;
    m.tex_slot = 1;
    m.uv_scale = 1.f;
  } else if (mesh_id == "terrain") {
    m.base_color = {0.28f, 0.42f, 0.22f, 1.f};
    m.roughness = 0.95f;
    m.metallic = 0.f;
    m.mesh_slot = 2;
    m.uv_scale = 1.f;
  } else if (mesh_id == "water") {
    m.base_color = {0.22f, 0.45f, 0.72f, 1.f};
    m.roughness = 0.08f;
    m.metallic = 0.05f;
    m.mesh_slot = 5;
    m.uv_scale = 1.f;
  } else if (mesh_id == "morph") {
    m.base_color = {0.95f, 0.55f, 0.35f, 1.f};
    m.roughness = 0.35f;
    m.metallic = 0.1f;
    m.mesh_slot = 3;
    m.uv_scale = 1.f;
  } else {
    m.base_color = {1.f, 1.f, 1.f, 1.f};
    m.roughness = 0.4f;
    m.metallic = 0.05f;
    m.albedo_tex = "textures/ph/brick_diff.jpg";
    m.orm_tex = "textures/ph/brick_arm.jpg";
    m.uv_scale = 2.f;
    m.tex_slot = 0;
  }
  return m;
}

Mat4 BuildLocalShadowMatrix(const LocalLight& light, const Vec3& look_at) {
  constexpr float kDegToRad = 0.01745329252f;
  Vec3 target = look_at;
  float fovy = 1.04719755f;  // ~60° default for non-spot helper path
  if (IsSpotLight(light)) {
    Vec3 dir = light.direction;
    if (dir.length_squared() < 1e-6f) {
      dir = Vec3{0.f, -1.f, 0.f};
    }
    dir = Normalize(dir);
    target = light.position + dir;
    // spot_angle_deg is half-angle from axis → full cone FOV = 2×.
    const float half = std::max(light.spot_angle_deg, 1.f) * kDegToRad;
    fovy = std::min(half * 2.f, 3.0f);
  } else {
    Vec3 to = target - light.position;
    if (to.length_squared() < 1e-6f) {
      target = light.position + Vec3{0.f, -1.f, 0.f};
    }
  }
  Vec3 up{0.f, 1.f, 0.f};
  const Vec3 forward = Normalize(target - light.position);
  if (std::fabs(Dot(forward, up)) > 0.95f) {
    up = Vec3{0.f, 0.f, 1.f};
  }
  const Mat4 view = Mat4::LookAt(light.position, target, up);
  const float z_far = std::max(light.range, 1.f);
  const Mat4 proj = Mat4::Perspective(fovy, 1.f, 0.05f, z_far);
  return proj * view;
}

std::array<Mat4, 6> BuildLocalShadowCubeMatrices(const LocalLight& light) {
  // Order: +X -X +Y -Y +Z -Z (must match lit_cube.hlsl face pick).
  const Vec3 dirs[6] = {
      {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
      {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f},
  };
  const Vec3 ups[6] = {
      {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},
      {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f},
  };
  const float z_far = std::max(light.range, 1.f);
  const Mat4 proj = Mat4::Perspective(1.570796327f, 1.f, 0.05f, z_far);  // 90°
  std::array<Mat4, 6> out{};
  for (int i = 0; i < 6; ++i) {
    const Vec3 target = light.position + dirs[i];
    out[static_cast<std::size_t>(i)] =
        proj * Mat4::LookAt(light.position, target, ups[i]);
  }
  return out;
}

}  // namespace engine::render
