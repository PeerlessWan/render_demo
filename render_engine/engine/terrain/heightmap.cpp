#include "engine/terrain/heightmap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::terrain {

float SampleHeight(const Heightmap& map, float x, float z) {
  if (map.width < 2 || map.height < 2 || map.samples.empty()) {
    return 0.f;
  }
  const float u = x / map.cell;
  const float v = z / map.cell;
  const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, map.width - 2);
  const int z0 = std::clamp(static_cast<int>(std::floor(v)), 0, map.height - 2);
  const float fx = u - static_cast<float>(x0);
  const float fz = v - static_cast<float>(z0);
  const auto at = [&](int ix, int iz) {
    return map.samples[static_cast<std::size_t>(iz * map.width + ix)];
  };
  const float h00 = at(x0, z0);
  const float h10 = at(x0 + 1, z0);
  const float h01 = at(x0, z0 + 1);
  const float h11 = at(x0 + 1, z0 + 1);
  const float hx0 = h00 + (h10 - h00) * fx;
  const float hx1 = h01 + (h11 - h01) * fx;
  return hx0 + (hx1 - hx0) * fz;
}

int SelectTerrainLod(float distance, const std::vector<float>& ranges) {
  for (int i = 0; i < static_cast<int>(ranges.size()); ++i) {
    if (distance < ranges[static_cast<std::size_t>(i)]) {
      return i;
    }
  }
  return static_cast<int>(ranges.size());
}

std::vector<VegetationInstance> ScatterVegetation(const Heightmap& map, float water_level,
                                                  int stride) {
  std::vector<VegetationInstance> out;
  if (stride < 1) {
    stride = 1;
  }
  for (int z = 0; z < map.height; z += stride) {
    for (int x = 0; x < map.width; x += stride) {
      const float h = map.samples[static_cast<std::size_t>(z * map.width + x)];
      if (h <= water_level) {
        continue;
      }
      VegetationInstance v;
      v.position = {static_cast<float>(x) * map.cell, h, static_cast<float>(z) * map.cell};
      v.scale = 0.8f + 0.4f * (h - water_level);
      v.type_id = (x + z) % 3;
      out.push_back(v);
    }
  }
  return out;
}

TerrainMesh BuildTerrainMesh(const Heightmap& map, const Vec3& world_origin) {
  TerrainMesh mesh;
  if (map.width < 2 || map.height < 2 || map.samples.empty()) {
    return mesh;
  }
  const int w = map.width;
  const int h = map.height;
  mesh.positions.resize(static_cast<std::size_t>(w * h * 3));
  mesh.normals.resize(static_cast<std::size_t>(w * h * 3));
  mesh.uvs.resize(static_cast<std::size_t>(w * h * 2));
  for (int z = 0; z < h; ++z) {
    for (int x = 0; x < w; ++x) {
      const std::size_t i = static_cast<std::size_t>(z * w + x);
      const float y = map.samples[i];
      mesh.positions[i * 3 + 0] = world_origin.x + static_cast<float>(x) * map.cell;
      mesh.positions[i * 3 + 1] = world_origin.y + y;
      mesh.positions[i * 3 + 2] = world_origin.z + static_cast<float>(z) * map.cell;
      mesh.uvs[i * 2 + 0] = static_cast<float>(x) / static_cast<float>(w - 1);
      mesh.uvs[i * 2 + 1] = static_cast<float>(z) / static_cast<float>(h - 1);

      const float hL = SampleHeight(map, (x - 1) * map.cell, z * map.cell);
      const float hR = SampleHeight(map, (x + 1) * map.cell, z * map.cell);
      const float hD = SampleHeight(map, x * map.cell, (z - 1) * map.cell);
      const float hU = SampleHeight(map, x * map.cell, (z + 1) * map.cell);
      Vec3 n = Normalize(Vec3{hL - hR, 2.f * map.cell, hD - hU});
      mesh.normals[i * 3 + 0] = n.x;
      mesh.normals[i * 3 + 1] = n.y;
      mesh.normals[i * 3 + 2] = n.z;
    }
  }
  mesh.indices.reserve(static_cast<std::size_t>((w - 1) * (h - 1) * 6));
  for (int z = 0; z < h - 1; ++z) {
    for (int x = 0; x < w - 1; ++x) {
      const std::uint32_t i0 = static_cast<std::uint32_t>(z * w + x);
      const std::uint32_t i1 = i0 + 1;
      const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(w);
      const std::uint32_t i3 = i2 + 1;
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i3);
    }
  }
  return mesh;
}

TerrainMesh BuildWaterPatchMesh(float half_extent) {
  TerrainMesh mesh;
  if (half_extent <= 0.f) {
    return mesh;
  }
  const float h = half_extent;
  mesh.positions = {-h, 0.f, -h, h, 0.f, -h, h, 0.f, h, -h, 0.f, h};
  mesh.normals = {0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f};
  mesh.uvs = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
  mesh.indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

}  // namespace engine::terrain
