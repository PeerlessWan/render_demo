#include "mini_test.h"

#include "engine/core/math.h"
#include "engine/render/local_lights.h"
#include "engine/rhi/i_device.h"

#include <algorithm>
#include <array>
#include <vector>

TEST_CASE("Z-slice binning separates near and far lights", "[m34][w10][c02]") {
  static_assert(engine::render::kLightZSlices == 4);
  static_assert(engine::render::kLightClusterCount == 128);
  static_assert(engine::render::kMaxLocalLightsGpu == 32);

  std::vector<engine::render::LocalLight> lights(2);
  lights[0].position = {0.f, 0.f, -4.f};   // view depth ~4 → slice 0
  lights[0].range = 0.05f;
  lights[1].position = {0.f, 0.f, -50.f};  // view depth ~50 → slice 2
  lights[1].range = 0.05f;

  const engine::Vec3 eye{0.f, 0.f, 0.f};
  const engine::Vec3 forward{0.f, 0.f, -1.f};
  const engine::Mat4 view = engine::Mat4::LookAt(eye, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::vector<std::vector<int>> tiles;
  engine::render::AssignLightsToTiles(lights, vp, engine::render::kLightTileGridW,
                                      engine::render::kLightTileGridH, tiles, eye, forward);
  REQUIRE(tiles.size() == static_cast<std::size_t>(engine::render::kLightClusterCount));

  const int slice_near = engine::render::ViewZToSlice(4.f);
  const int slice_far = engine::render::ViewZToSlice(50.f);
  REQUIRE(slice_near != slice_far);
  REQUIRE(slice_near == 0);
  REQUIRE(slice_far >= 2);

  bool near_in_near_slice = false;
  bool near_in_far_slice = false;
  bool far_in_far_slice = false;
  bool far_in_near_slice = false;
  for (int t = 0; t < engine::render::kLightTileCount; ++t) {
    for (int idx : tiles[static_cast<std::size_t>(slice_near * engine::render::kLightTileCount + t)]) {
      if (idx == 0) {
        near_in_near_slice = true;
      }
      if (idx == 1) {
        far_in_near_slice = true;
      }
    }
    for (int idx : tiles[static_cast<std::size_t>(slice_far * engine::render::kLightTileCount + t)]) {
      if (idx == 0) {
        near_in_far_slice = true;
      }
      if (idx == 1) {
        far_in_far_slice = true;
      }
    }
  }
  REQUIRE(near_in_near_slice);
  REQUIRE(far_in_far_slice);
  REQUIRE_FALSE(near_in_far_slice);
  REQUIRE_FALSE(far_in_near_slice);
}

TEST_CASE("PackTileLightLists accepts 32 lights across clusters", "[m34][w10][c02]") {
  std::vector<engine::render::LocalLight> lights(32);
  for (int i = 0; i < 32; ++i) {
    const float x = -3.5f + static_cast<float>(i % 8) * 1.f;
    const float z = -4.f - static_cast<float>(i / 8) * 12.f;
    lights[static_cast<std::size_t>(i)].position = {x, 0.f, z};
    lights[static_cast<std::size_t>(i)].range = 1.5f;
  }
  const engine::Vec3 eye{0.f, 0.f, 0.f};
  const engine::Vec3 forward{0.f, 0.f, -1.f};
  const engine::Mat4 view = engine::Mat4::LookAt(eye, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::vector<std::vector<int>> tiles;
  engine::render::AssignLightsToTiles(lights, vp, engine::render::kLightTileGridW,
                                      engine::render::kLightTileGridH, tiles, eye, forward);
  std::array<int, engine::render::kLightClusterCount> counts{};
  std::array<int, engine::render::kTileLightIndexCount> indices{};
  engine::render::PackTileLightLists(tiles, counts, indices);

  int populated = 0;
  int max_idx = -1;
  for (int c = 0; c < engine::render::kLightClusterCount; ++c) {
    const int n = counts[static_cast<std::size_t>(c)];
    REQUIRE(n <= engine::render::kMaxLightsPerTile);
    if (n > 0) {
      ++populated;
    }
    for (int s = 0; s < n; ++s) {
      const int idx =
          indices[static_cast<std::size_t>(c * engine::render::kMaxLightsPerTile + s)];
      REQUIRE(idx >= 0);
      REQUIRE(idx < 32);
      max_idx = (std::max)(max_idx, idx);
    }
  }
  REQUIRE(populated > 0);
  REQUIRE(max_idx >= 16);  // pack reaches into the upper half of the 32-light set

  engine::rhi::FrameLighting lighting{};
  lighting.local_light_count = 32;
  lighting.enable_tiled_lights = true;
  for (int i = 0; i < engine::render::kLightClusterCount; ++i) {
    lighting.tile_light_count[static_cast<std::size_t>(i)] = counts[static_cast<std::size_t>(i)];
  }
  for (int i = 0; i < engine::render::kTileLightIndexCount; ++i) {
    lighting.tile_light_index[static_cast<std::size_t>(i)] = indices[static_cast<std::size_t>(i)];
  }
  REQUIRE(lighting.local_pos.size() == 32);
  REQUIRE(lighting.tile_light_count.size() == 128);
  REQUIRE(lighting.tile_light_index.size() == 1024);
}

TEST_CASE("SimulateLightTileCullCs matches Assign with Z-slices", "[m34][w10][c02]") {
  std::vector<engine::render::LocalLight> lights(2);
  lights[0].position = {-1.f, 0.f, -5.f};
  lights[0].range = 2.f;
  lights[1].position = {1.f, 0.f, -40.f};
  lights[1].range = 3.f;
  const engine::Vec3 eye{0.f, 0.f, 0.f};
  const engine::Vec3 forward{0.f, 0.f, -1.f};
  const engine::Mat4 view = engine::Mat4::LookAt(eye, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::vector<std::vector<int>> tiles;
  engine::render::AssignLightsToTiles(lights, vp, engine::render::kLightTileGridW,
                                      engine::render::kLightTileGridH, tiles, eye, forward);
  std::array<int, engine::render::kLightClusterCount> ref_counts{};
  std::array<int, engine::render::kTileLightIndexCount> ref_indices{};
  engine::render::PackTileLightLists(tiles, ref_counts, ref_indices);

  std::array<engine::Vec3, 2> positions{lights[0].position, lights[1].position};
  std::array<float, 2> ranges{lights[0].range, lights[1].range};
  std::array<int, engine::render::kLightClusterCount> sim_counts{};
  std::array<int, engine::render::kTileLightIndexCount> sim_indices{};
  engine::render::SimulateLightTileCullCs(vp, positions, ranges, sim_counts, sim_indices, eye,
                                          forward);
  REQUIRE(sim_counts == ref_counts);
  REQUIRE(sim_indices == ref_indices);
}
