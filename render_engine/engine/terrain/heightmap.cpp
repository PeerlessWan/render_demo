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
  return BuildAnimatedWaterPatchMesh(half_extent, 1, 0.f, 0.f);
}

void AnimateWaterPatch(TerrainMesh& mesh, float time, float amplitude, float wavelength,
                       float speed) {
  if (mesh.positions.size() < 3 || wavelength <= 1e-4f) {
    return;
  }
  const float k = 6.2831853f / wavelength;
  const std::size_t n = mesh.positions.size() / 3;
  mesh.normals.assign(n * 3, 0.f);
  for (std::size_t i = 0; i < n; ++i) {
    float& x = mesh.positions[i * 3 + 0];
    float& y = mesh.positions[i * 3 + 1];
    float& z = mesh.positions[i * 3 + 2];
    const float phase = k * (x + 0.65f * z) - speed * time;
    y = amplitude * std::sin(phase);
    // Analytic Gerstner-ish normal (small amplitude).
    const float ddy_dx = amplitude * k * std::cos(phase);
    const float ddy_dz = amplitude * k * 0.65f * std::cos(phase);
    Vec3 nrm{-ddy_dx, 1.f, -ddy_dz};
    const float len = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
    if (len > 1e-6f) {
      nrm.x /= len;
      nrm.y /= len;
      nrm.z /= len;
    }
    mesh.normals[i * 3 + 0] = nrm.x;
    mesh.normals[i * 3 + 1] = nrm.y;
    mesh.normals[i * 3 + 2] = nrm.z;
  }
}

TerrainMesh BuildAnimatedWaterPatchMesh(float half_extent, int segments, float time,
                                        float amplitude) {
  TerrainMesh mesh;
  if (half_extent <= 0.f) {
    return mesh;
  }
  const int seg = std::max(1, segments);
  const int verts = seg + 1;
  mesh.positions.reserve(static_cast<std::size_t>(verts * verts * 3));
  mesh.uvs.reserve(static_cast<std::size_t>(verts * verts * 2));
  for (int z = 0; z < verts; ++z) {
    const float tz = static_cast<float>(z) / static_cast<float>(seg);
    const float wz = -half_extent + 2.f * half_extent * tz;
    for (int x = 0; x < verts; ++x) {
      const float tx = static_cast<float>(x) / static_cast<float>(seg);
      const float wx = -half_extent + 2.f * half_extent * tx;
      mesh.positions.push_back(wx);
      mesh.positions.push_back(0.f);
      mesh.positions.push_back(wz);
      mesh.uvs.push_back(tx);
      mesh.uvs.push_back(tz);
    }
  }
  mesh.indices.reserve(static_cast<std::size_t>(seg * seg * 6));
  for (int z = 0; z < seg; ++z) {
    for (int x = 0; x < seg; ++x) {
      const std::uint32_t i0 = static_cast<std::uint32_t>(z * verts + x);
      const std::uint32_t i1 = i0 + 1;
      const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(verts);
      const std::uint32_t i3 = i2 + 1;
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i2);
    }
  }
  AnimateWaterPatch(mesh, time, amplitude);
  return mesh;
}

float SampleHeightTiled(const Heightmap& map, float x, float z, float origin_x, float origin_z) {
  if (map.width < 2 || map.height < 2 || map.samples.empty() || map.cell <= 1e-8f) {
    return 0.f;
  }
  const float extent_x = static_cast<float>(map.width - 1) * map.cell;
  const float extent_z = static_cast<float>(map.height - 1) * map.cell;
  auto wrap = [](float v, float extent) {
    float t = std::fmod(v, extent);
    if (t < 0.f) {
      t += extent;
    }
    return t;
  };
  return SampleHeight(map, wrap(x - origin_x, extent_x), wrap(z - origin_z, extent_z));
}

void AnimateWaterPatchFromHeightfield(TerrainMesh& mesh, const Heightmap& field, float origin_x,
                                      float origin_z, float foam_scale) {
  if (mesh.positions.size() < 3 || field.width < 2 || field.height < 2) {
    return;
  }
  const std::size_t n = mesh.positions.size() / 3;
  mesh.normals.assign(n * 3, 0.f);
  if (mesh.uvs.size() < n * 2) {
    mesh.uvs.assign(n * 2, 0.f);
  }
  const float eps = std::max(field.cell, 1e-3f);
  for (std::size_t i = 0; i < n; ++i) {
    float& x = mesh.positions[i * 3 + 0];
    float& y = mesh.positions[i * 3 + 1];
    float& z = mesh.positions[i * 3 + 2];
    y = SampleHeightTiled(field, x, z, origin_x, origin_z);
    const float hL = SampleHeightTiled(field, x - eps, z, origin_x, origin_z);
    const float hR = SampleHeightTiled(field, x + eps, z, origin_x, origin_z);
    const float hD = SampleHeightTiled(field, x, z - eps, origin_x, origin_z);
    const float hU = SampleHeightTiled(field, x, z + eps, origin_x, origin_z);
    Vec3 nrm = Normalize(Vec3{hL - hR, 2.f * eps, hD - hU});
    mesh.normals[i * 3 + 0] = nrm.x;
    mesh.normals[i * 3 + 1] = nrm.y;
    mesh.normals[i * 3 + 2] = nrm.z;
    const float dx = (hR - hL) / (2.f * eps);
    const float dz = (hU - hD) / (2.f * eps);
    const float slope = std::sqrt(dx * dx + dz * dz);
    mesh.uvs[i * 2 + 0] = std::clamp(slope * foam_scale - 0.15f, 0.f, 1.f);
  }
}

}  // namespace engine::terrain
