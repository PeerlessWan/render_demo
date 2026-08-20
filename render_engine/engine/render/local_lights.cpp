#include "engine/render/local_lights.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::render {
namespace {

// Project world point to UV in [0,1] (NDC xy * 0.5 + 0.5). Rejects behind-camera / non-finite.
bool ProjectWorldToUv(const Mat4& view_proj, const Vec3& p, float& out_u, float& out_v) {
  const float x = view_proj.m[0] * p.x + view_proj.m[4] * p.y + view_proj.m[8] * p.z + view_proj.m[12];
  const float y = view_proj.m[1] * p.x + view_proj.m[5] * p.y + view_proj.m[9] * p.z + view_proj.m[13];
  const float w = view_proj.m[3] * p.x + view_proj.m[7] * p.y + view_proj.m[11] * p.z + view_proj.m[15];
  if (!(w > 1e-5f)) {
    return false;
  }
  const float ndc_x = x / w;
  const float ndc_y = y / w;
  if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) {
    return false;
  }
  out_u = ndc_x * 0.5f + 0.5f;
  out_v = ndc_y * 0.5f + 0.5f;
  return std::isfinite(out_u) && std::isfinite(out_v);
}

// Sphere → screen AABB via center + ±range on XYZ (matches light_tile_cull_cs.hlsl).
bool LightProjectedUvAabb(const Vec3& position, float range, const Mat4& view_proj, float& u0,
                          float& u1, float& v0, float& v1) {
  const float r = std::max(range, 0.f);
  const Vec3 offsets[7] = {
      {0.f, 0.f, 0.f}, {r, 0.f, 0.f}, {-r, 0.f, 0.f}, {0.f, r, 0.f},
      {0.f, -r, 0.f},  {0.f, 0.f, r}, {0.f, 0.f, -r},
  };
  u0 = 1e9f;
  u1 = -1e9f;
  v0 = 1e9f;
  v1 = -1e9f;
  int accepted = 0;
  for (const Vec3& o : offsets) {
    float u = 0.f;
    float v = 0.f;
    if (!ProjectWorldToUv(view_proj, position + o, u, v)) {
      continue;
    }
    u0 = (std::min)(u0, u);
    u1 = (std::max)(u1, u);
    v0 = (std::min)(v0, v);
    v1 = (std::max)(v1, v);
    ++accepted;
  }
  return accepted > 0;
}

float ViewDepth(const Vec3& world, const Vec3& eye, const Vec3& forward_n) {
  return Dot(world - eye, forward_n);
}

}  // namespace

void AssignLightsToTiles(const std::vector<LocalLight>& lights, const Mat4& view_proj,
                         int grid_w, int grid_h,
                         std::vector<std::vector<int>>& out_tiles, const Vec3& eye,
                         const Vec3& cam_forward, float z_near, float z_far) {
  const int gw = std::max(grid_w, 1);
  const int gh = std::max(grid_h, 1);
  const int tile_count = gw * gh;
  const int cluster_count = tile_count * kLightZSlices;
  out_tiles.assign(static_cast<std::size_t>(cluster_count), {});
  Vec3 fwd = cam_forward;
  if (fwd.length_squared() < 1e-12f) {
    fwd = Vec3{0.f, 0.f, -1.f};
  }
  fwd = Normalize(fwd);
  for (std::size_t i = 0; i < lights.size(); ++i) {
    float u0 = 0.f;
    float u1 = 0.f;
    float v0 = 0.f;
    float v1 = 0.f;
    if (!LightProjectedUvAabb(lights[i].position, lights[i].range, view_proj, u0, u1, v0, v1)) {
      continue;
    }
    // Fully off-screen AABB → skip.
    if (u1 < 0.f || u0 > 1.f || v1 < 0.f || v0 > 1.f) {
      continue;
    }
    const float cu0 = std::clamp(u0, 0.f, 0.999f);
    const float cu1 = std::clamp(u1, 0.f, 0.999f);
    const float cv0 = std::clamp(v0, 0.f, 0.999f);
    const float cv1 = std::clamp(v1, 0.f, 0.999f);
    const int tx0 = std::min(static_cast<int>(cu0 * static_cast<float>(gw)), gw - 1);
    const int tx1 = std::min(static_cast<int>(cu1 * static_cast<float>(gw)), gw - 1);
    const int ty0 = std::min(static_cast<int>(cv0 * static_cast<float>(gh)), gh - 1);
    const int ty1 = std::min(static_cast<int>(cv1 * static_cast<float>(gh)), gh - 1);

    const float r = std::max(lights[i].range, 0.f);
    const float vz = ViewDepth(lights[i].position, eye, fwd);
    const int sz0 = ViewZToSlice(vz - r, z_near, z_far);
    const int sz1 = ViewZToSlice(vz + r, z_near, z_far);
    for (int sz = sz0; sz <= sz1; ++sz) {
      for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
          const int cluster = sz * tile_count + ty * gw + tx;
          out_tiles[static_cast<std::size_t>(cluster)].push_back(static_cast<int>(i));
        }
      }
    }
  }
}

void PackTileLightLists(const std::vector<std::vector<int>>& tiles,
                        std::array<int, kLightClusterCount>& out_counts,
                        std::array<int, kTileLightIndexCount>& out_indices) {
  out_counts.fill(0);
  out_indices.fill(-1);
  const std::size_t n = (std::min)(tiles.size(), static_cast<std::size_t>(kLightClusterCount));
  for (std::size_t t = 0; t < n; ++t) {
    const auto& list = tiles[t];
    const int count =
        static_cast<int>((std::min)(list.size(), static_cast<std::size_t>(kMaxLightsPerTile)));
    out_counts[t] = count;
    for (int s = 0; s < count; ++s) {
      out_indices[t * static_cast<std::size_t>(kMaxLightsPerTile) + static_cast<std::size_t>(s)] =
          list[static_cast<std::size_t>(s)];
    }
  }
}

void SimulateLightTileCullCs(const Mat4& view_proj, std::span<const Vec3> positions,
                             std::span<const float> ranges,
                             std::array<int, kLightClusterCount>& out_counts,
                             std::array<int, kTileLightIndexCount>& out_indices, const Vec3& eye,
                             const Vec3& cam_forward, float z_near, float z_far) {
  const std::size_t n =
      (std::min)((std::min)(positions.size(), ranges.size()),
                 static_cast<std::size_t>(kMaxLocalLightsGpu));
  std::vector<LocalLight> lights(n);
  for (std::size_t i = 0; i < n; ++i) {
    lights[i].position = positions[i];
    lights[i].range = ranges[i];
  }
  std::vector<std::vector<int>> tiles;
  AssignLightsToTiles(lights, view_proj, kLightTileGridW, kLightTileGridH, tiles, eye,
                      cam_forward, z_near, z_far);
  PackTileLightLists(tiles, out_counts, out_indices);
}

void EvalTiledLightList(const std::array<int, kLightClusterCount>& counts,
                        const std::array<int, kTileLightIndexCount>& indices, float u, float v,
                        float view_z, std::vector<int>& out_lights, float z_near, float z_far) {
  out_lights.clear();
  const float cu = std::clamp(u, 0.f, 0.999f);
  const float cv = std::clamp(v, 0.f, 0.999f);
  const int tx =
      std::min(static_cast<int>(cu * static_cast<float>(kLightTileGridW)), kLightTileGridW - 1);
  const int ty =
      std::min(static_cast<int>(cv * static_cast<float>(kLightTileGridH)), kLightTileGridH - 1);
  const int tile = ty * kLightTileGridW + tx;
  const int slice = ViewZToSlice(view_z, z_near, z_far);
  const int cluster = slice * kLightTileCount + tile;
  const int count = std::clamp(counts[static_cast<std::size_t>(cluster)], 0, kMaxLightsPerTile);
  out_lights.reserve(static_cast<std::size_t>(count));
  for (int s = 0; s < count; ++s) {
    const int idx =
        indices[static_cast<std::size_t>(cluster * kMaxLightsPerTile + s)];
    if (idx >= 0) {
      out_lights.push_back(idx);
    }
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
    // Mesh slot1 geometry; albedo shares physical tex slot1 with suburb colormap.
    // Prefer suburb (town is the Sandbox hero). Helmet stays lit via base_color only.
    m.base_color = {0.72f, 0.55f, 0.42f, 1.f};
    m.roughness = 0.45f;
    m.metallic = 0.35f;
    m.mesh_slot = 1;
    m.tex_slot = 0;
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
  } else if (mesh_id == "suburb") {
    m.base_color = {1.f, 1.f, 1.f, 1.f};
    m.roughness = 0.75f;
    m.metallic = 0.05f;
    m.albedo_tex = "scenes/suburb/models/Textures/colormap.png";
    m.mesh_slot = 6;
    // Secondary albedo is physical slot1 (shader: tex_slot>0.5 → albedo_map2). Not a 3rd atlas.
    m.tex_slot = 1;
    m.uv_scale = 1.f;
  } else if (mesh_id == "character" || mesh_id.rfind("character_", 0) == 0) {
    m.base_color = {0.55f, 0.72f, 0.45f, 1.f};
    m.roughness = 0.7f;
    m.metallic = 0.02f;
    m.mesh_slot = 7;
    m.tex_slot = 0;
    m.uv_scale = 1.f;
    // W17: character_8 / character_9 … → mesh slot from suffix.
    if (mesh_id.size() > 10 && mesh_id[9] == '_') {
      int slot = 0;
      for (std::size_t i = 10; i < mesh_id.size(); ++i) {
        const char c = mesh_id[i];
        if (c < '0' || c > '9') {
          slot = 0;
          break;
        }
        slot = slot * 10 + (c - '0');
      }
      if (slot > 0 && slot < 16) {
        m.mesh_slot = slot;
      }
      // W20: WorldText (13) / HLOD impostor (15) sample secondary albedo atlas (slot=1).
      if (slot == 13 || slot == 15) {
        m.base_color = {1.f, 1.f, 1.f, 1.f};
        m.albedo_tex = (slot == 13) ? "world_text_atlas" : "hlod_impostor";
        m.tex_slot = 1;
      }
    }
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
