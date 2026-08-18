#include "editing/terrain_edit.h"

#include "engine/core/math.h"

#include <algorithm>
#include <cmath>

namespace editor {

void EnsureHeights(std::vector<float>* heights) {
  if (!heights) {
    return;
  }
  if (heights->size() != static_cast<std::size_t>(kSculptRes * kSculptRes)) {
    heights->assign(static_cast<std::size_t>(kSculptRes * kSculptRes), 0.f);
  }
}

void RaiseHeight(std::vector<float>* heights, int x, int z, float amount, float radius) {
  EnsureHeights(heights);
  const int n = kSculptRes;
  x = std::clamp(x, 0, n - 1);
  z = std::clamp(z, 0, n - 1);
  const float r = std::max(radius, 0.5f);
  for (int zz = 0; zz < n; ++zz) {
    for (int xx = 0; xx < n; ++xx) {
      const float dx = static_cast<float>(xx - x);
      const float dz = static_cast<float>(zz - z);
      const float d = std::sqrt(dx * dx + dz * dz);
      if (d > r) {
        continue;
      }
      const float w = 1.f - (d / r);
      (*heights)[static_cast<std::size_t>(zz * n + xx)] += amount * w * w;
    }
  }
}

void LowerHeight(std::vector<float>* heights, int x, int z, float amount, float radius) {
  RaiseHeight(heights, x, z, -amount, radius);
}

void SmoothHeight(std::vector<float>* heights, int x, int z, float radius) {
  EnsureHeights(heights);
  const int n = kSculptRes;
  x = std::clamp(x, 0, n - 1);
  z = std::clamp(z, 0, n - 1);
  const float r = std::max(radius, 0.5f);
  std::vector<float> next = *heights;
  for (int zz = 0; zz < n; ++zz) {
    for (int xx = 0; xx < n; ++xx) {
      const float dx = static_cast<float>(xx - x);
      const float dz = static_cast<float>(zz - z);
      if (std::sqrt(dx * dx + dz * dz) > r) {
        continue;
      }
      float sum = 0.f;
      int count = 0;
      for (int oz = -1; oz <= 1; ++oz) {
        for (int ox = -1; ox <= 1; ++ox) {
          const int nx = xx + ox;
          const int nz = zz + oz;
          if (nx < 0 || nz < 0 || nx >= n || nz >= n) {
            continue;
          }
          sum += (*heights)[static_cast<std::size_t>(nz * n + nx)];
          ++count;
        }
      }
      if (count > 0) {
        next[static_cast<std::size_t>(zz * n + xx)] = sum / static_cast<float>(count);
      }
    }
  }
  *heights = std::move(next);
}

engine::terrain::Heightmap HeightsToMap(const std::vector<float>& heights, float cell) {
  engine::terrain::Heightmap map;
  map.width = kSculptRes;
  map.height = kSculptRes;
  map.cell = cell;
  map.samples = heights;
  if (map.samples.size() != static_cast<std::size_t>(kSculptRes * kSculptRes)) {
    map.samples.assign(static_cast<std::size_t>(kSculptRes * kSculptRes), 0.f);
  }
  return map;
}

bool UploadTerrainMesh(engine::rhi::IDevice& device, engine::scene::World& world,
                       const std::vector<float>& heights, int mesh_slot) {
  const auto map = HeightsToMap(heights, 1.f);
  const engine::Vec3 origin{-8.f, 0.f, -8.f};
  const auto mesh = engine::terrain::BuildTerrainMesh(map, origin);
  if (mesh.indices.empty() || mesh.positions.size() < 3) {
    return false;
  }
  std::vector<engine::rhi::LitVertex> verts(mesh.positions.size() / 3);
  for (std::size_t i = 0; i < verts.size(); ++i) {
    verts[i] = {mesh.positions[i * 3 + 0], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2],
                mesh.normals[i * 3 + 0],   mesh.normals[i * 3 + 1],   mesh.normals[i * 3 + 2],
                mesh.uvs[i * 2 + 0],       mesh.uvs[i * 2 + 1]};
  }
  if (!device.UploadLitGeometry(mesh_slot, verts, mesh.indices)) {
    return false;
  }
  engine::scene::NodeId terrain = engine::scene::kInvalidNode;
  for (auto r : world.roots()) {
    if (world.name(r) == "terrain") {
      terrain = r;
      break;
    }
  }
  if (!world.valid(terrain)) {
    terrain = world.CreateNode("terrain");
  }
  engine::scene::MeshRenderer mr;
  mr.mesh_id = "terrain";
  mr.never_cull = true;
  mr.local_bounds = {{-8.f, -2.f, -8.f}, {8.f, 8.f, 8.f}};
  world.set_mesh(terrain, mr);
  return true;
}

}  // namespace editor
