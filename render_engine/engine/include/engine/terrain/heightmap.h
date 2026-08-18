#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <filesystem>
#include <vector>

namespace engine::terrain {

struct Heightmap {
  int width = 0;
  int height = 0;
  float cell = 1.f;
  std::vector<float> samples;  // row-major
};

// Mega-W10: load grayscale/RGB PNG (stb_image) into float samples in [0, height_scale].
// Supports 512 / 1024 / larger; R channel (or gray) drives height. Fail if decode unavailable.
[[nodiscard]] Result<Heightmap> LoadHeightmapPng(const std::filesystem::path& path,
                                                 float cell = 1.f, float height_scale = 1.f);

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

// W6/C09: Gerstner-like height displace on an existing water grid (mutates Y + normals).
// segments: grid resolution along one edge (>=1). time / amplitude / wavelength drive waves.
void AnimateWaterPatch(TerrainMesh& mesh, float time, float amplitude = 0.15f,
                       float wavelength = 4.f, float speed = 1.2f);

// Build a subdivided water grid then optionally animate (convenience for Sandbox/tests).
TerrainMesh BuildAnimatedWaterPatchMesh(float half_extent, int segments, float time,
                                        float amplitude = 0.15f);

// Mega-W8: sample a periodic heightfield with world origin (infinite tiling helper).
float SampleHeightTiled(const Heightmap& map, float x, float z, float origin_x, float origin_z);

// Displace water mesh Y/normals from a heightmap (world XZ), foam packed into UV.x from slope.
void AnimateWaterPatchFromHeightfield(TerrainMesh& mesh, const Heightmap& field, float origin_x,
                                      float origin_z, float foam_scale = 2.5f);

}  // namespace engine::terrain
