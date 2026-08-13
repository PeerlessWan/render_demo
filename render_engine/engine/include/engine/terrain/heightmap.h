#pragma once

#include "engine/core/math.h"

#include <vector>

namespace engine::terrain {

struct Heightmap {
  int width = 0;
  int height = 0;
  float cell = 1.f;
  std::vector<float> samples;  // row-major
};

float SampleHeight(const Heightmap& map, float x, float z);
int SelectTerrainLod(float distance, const std::vector<float>& ranges);

struct WaterPatch {
  Vec3 center{};
  float size = 10.f;
  float level = 0.f;
};

struct VegetationInstance {
  Vec3 position{};
  float scale = 1.f;
  int type_id = 0;
};

// Scatter simple vegetation on heightmap above water.
std::vector<VegetationInstance> ScatterVegetation(const Heightmap& map, float water_level,
                                                  int stride);

}  // namespace engine::terrain
