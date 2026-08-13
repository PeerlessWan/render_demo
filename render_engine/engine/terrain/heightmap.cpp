#include "engine/terrain/heightmap.h"

#include <algorithm>
#include <cmath>

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

}  // namespace engine::terrain
