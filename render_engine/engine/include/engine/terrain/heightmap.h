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

struct TerrainMesh {
  std::vector<float> positions;  // xyz packed
  std::vector<float> normals;    // xyz packed
  std::vector<float> uvs;        // uv packed
  std::vector<std::uint32_t> indices;
};

// Build a simple grid mesh from height samples (M23 deepen).
TerrainMesh BuildTerrainMesh(const Heightmap& map, const Vec3& world_origin);

// Flat water patch centered at origin, extent [-half_extent, half_extent] on XZ.
TerrainMesh BuildWaterPatchMesh(float half_extent);

}  // namespace engine::terrain
