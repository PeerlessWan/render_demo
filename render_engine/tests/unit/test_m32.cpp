#include "mini_test.h"

#include "engine/assets/streaming_budget.h"
#include "engine/core/feature.h"
#include "engine/core/math.h"
#include "engine/gi/probe_volume.h"
#include "engine/hlod/billboard_impostor.h"
#include "engine/render/light_function.h"
#include "engine/render/local_lights.h"
#include "engine/render2d/path2d.h"
#include "engine/rhi/i_device.h"
#include "engine/terrain/chunk_stream.h"
#include "engine/vt/virtual_texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

TEST_CASE("AssignLightsToTiles range expands across tiles", "[m32][w9][c02]") {
  std::vector<engine::render::LocalLight> lights(1);
  // Offset from exact screen center so a near-zero AABB does not straddle a tile edge.
  lights[0].position = {-1.2f, 0.3f, -5.f};
  lights[0].range = 0.01f;
  const engine::Mat4 view = engine::Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::vector<std::vector<int>> tiny;
  engine::render::AssignLightsToTiles(lights, vp, engine::render::kLightTileGridW,
                                      engine::render::kLightTileGridH, tiny);
  int tiny_hits = 0;
  for (const auto& t : tiny) {
    tiny_hits += static_cast<int>(t.size());
  }
  REQUIRE(tiny_hits == 1);

  lights[0].range = 12.f;
  std::vector<std::vector<int>> wide;
  engine::render::AssignLightsToTiles(lights, vp, engine::render::kLightTileGridW,
                                      engine::render::kLightTileGridH, wide);
  int wide_hits = 0;
  for (const auto& t : wide) {
    wide_hits += static_cast<int>(t.size());
  }
  REQUIRE(wide_hits > tiny_hits);
}

TEST_CASE("SimulateLightTileCullCs matches AssignLightsToTiles pack", "[m32][w9][c02]") {
  std::vector<engine::render::LocalLight> lights(3);
  lights[0].position = {-2.f, 0.f, -5.f};
  lights[0].range = 4.f;
  lights[1].position = {2.f, 0.f, -5.f};
  lights[1].range = 4.f;
  lights[2].position = {0.f, 1.f, -6.f};
  lights[2].range = 2.f;
  const engine::Mat4 view = engine::Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::vector<std::vector<int>> tiles;
  engine::render::CullLightsToTilesCpuReference(lights, vp, engine::render::kLightTileGridW,
                                                engine::render::kLightTileGridH, tiles);
  std::array<int, engine::render::kLightClusterCount> ref_counts{};
  std::array<int, engine::render::kTileLightIndexCount> ref_indices{};
  engine::render::PackTileLightLists(tiles, ref_counts, ref_indices);

  std::array<engine::Vec3, 3> positions{lights[0].position, lights[1].position,
                                       lights[2].position};
  std::array<float, 3> ranges{lights[0].range, lights[1].range, lights[2].range};
  std::array<int, engine::render::kLightClusterCount> sim_counts{};
  std::array<int, engine::render::kTileLightIndexCount> sim_indices{};
  engine::render::SimulateLightTileCullCs(vp, positions, ranges, sim_counts, sim_indices);

  REQUIRE(sim_counts == ref_counts);
  REQUIRE(sim_indices == ref_indices);
}

TEST_CASE("IDevice light tile cull Unavailable until Setup", "[m32][w9][c02]") {
  engine::rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.headless = true;
  auto dev = engine::rhi::CreateHeadlessDevice(desc);
  REQUIRE(dev);
  std::array<int, 128> counts{};
  std::array<int, 1024> indices{};
  std::array<engine::Vec3, 1> pos{{{0.f, 0.f, -5.f}}};
  std::array<float, 1> ranges{{2.f}};
  const engine::Mat4 vp = engine::Mat4::Identity();
  auto st = dev.value()->DispatchLightTileCull(vp, pos, ranges, counts, indices);
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
}

TEST_CASE("EvalTiledLightList sees range-binned neighbor tiles", "[m32][w9][c02]") {
  std::vector<engine::render::LocalLight> lights(1);
  lights[0].position = {-1.5f, 0.f, -5.f};
  lights[0].range = 8.f;
  const engine::Mat4 view = engine::Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::array<int, engine::render::kLightClusterCount> counts{};
  std::array<int, engine::render::kTileLightIndexCount> indices{};
  engine::render::SimulateLightTileCullCs(vp, std::span<const engine::Vec3>(&lights[0].position, 1),
                                          std::span<const float>(&lights[0].range, 1), counts,
                                          indices);

  int tiles_with_light = 0;
  for (int t = 0; t < engine::render::kLightClusterCount; ++t) {
    if (counts[static_cast<std::size_t>(t)] > 0) {
      ++tiles_with_light;
    }
  }
  REQUIRE(tiles_with_light >= 2);
}

TEST_CASE("VT GPU feedback stub and UploadPendingPages", "[m32][w9][c06]") {
  REQUIRE(engine::QueryFeature("virtual_texture"));

  engine::vt::VirtualTexture vt;
  vt.Configure(/*virtual_pages_xy=*/4, /*physical_slots=*/3, /*mip_count=*/1);
  REQUIRE(vt.resident_count() == 0);

  const std::vector<engine::vt::VtFeedbackRequest> feedback = {
      {engine::vt::PageCoord{0, 0, 0}, 2.f},
      {engine::vt::PageCoord{1, 0, 0}, 1.f},
      {engine::vt::PageCoord{0, 0, 0}, 0.5f},  // dup
  };
  vt.ProcessGpuFeedback(feedback);
  REQUIRE(vt.pending_requests().size() == 2);

  REQUIRE(vt.UploadPendingPages(1) == 1);
  REQUIRE(vt.IsResident(engine::vt::PageCoord{0, 0, 0}));
  REQUIRE(vt.UploadPendingPages(2) == 1);
  REQUIRE(vt.resident_count() == 2);

  const auto hit = vt.Sample(0.1f, 0.1f, 0);
  REQUIRE(hit.a == 1.f);

  // W18: packed u32 ingest (GPU readback word shape).
  const std::uint32_t packed = (2u) | (1u << 10) | (0u << 20) | (180u << 24);
  REQUIRE(vt.IngestFeedbackPackedU32({&packed, 1}) == 1);
  REQUIRE(vt.ProcessRequests(4) >= 1);
  std::vector<std::uint8_t> atlas;
  int aw = 0;
  int ah = 0;
  REQUIRE(vt.BuildPhysicalAtlasRgba(4, atlas, aw, ah));
  REQUIRE(aw > 0);
  REQUIRE(ah > 0);
  REQUIRE(atlas.size() == static_cast<std::size_t>(aw * ah * 4));
}

TEST_CASE("BillboardImpostor SwitchLod and Bake placeholder", "[m32][w9][c07]") {
  engine::hlod::BillboardImpostor lod;
  lod.distance_threshold = 20.f;
  lod.exit_distance = 15.f;
  lod.near_mesh_id = "mesh/tree_hi";
  REQUIRE(lod.SwitchLod(5.f) == engine::hlod::LodMode::NearMesh);
  REQUIRE(lod.SwitchLod(20.f) == engine::hlod::LodMode::Impostor);
  // Hysteresis: stay Impostor between exit and enter.
  REQUIRE(lod.SwitchLod(17.f) == engine::hlod::LodMode::Impostor);
  REQUIRE(lod.SwitchLod(14.f) == engine::hlod::LodMode::NearMesh);
  REQUIRE(lod.SwitchLod(100.f) == engine::hlod::LodMode::Impostor);

  const auto tex = engine::hlod::BakeImpostorPlaceholder({1.f, 0.f, 0.f, 1.f});
  REQUIRE(tex == "impostor/solid_ff0000ff");
  lod.impostor_tex_id = tex;
  REQUIRE_FALSE(lod.impostor_tex_id.empty());
}

TEST_CASE("TerrainChunkStreamer Load Unload with StreamingBudget", "[m32][w9][terrain]") {
  engine::terrain::TerrainChunkStreamer streamer;
  streamer.Configure(/*chunk_world_size=*/16.f, /*load_radius=*/1, /*bytes_per_chunk=*/100);
  engine::assets::StreamingBudget budget(10 * 100);

  streamer.Update({8.f, 0.f, 8.f}, budget);  // chunk (0,0), ring 3x3 = 9
  const engine::terrain::ChunkKey cam0 = streamer.last_camera_chunk();
  REQUIRE(cam0.x == 0);
  REQUIRE(cam0.z == 0);
  REQUIRE(streamer.resident_count() == 9);
  REQUIRE(streamer.load_count() == 9);
  REQUIRE(budget.used() == 9 * 100);

  streamer.ResetCounters();
  streamer.Update({40.f, 0.f, 8.f}, budget);  // chunk (2,0)
  const engine::terrain::ChunkKey cam2 = streamer.last_camera_chunk();
  REQUIRE(cam2.x == 2);
  REQUIRE(cam2.z == 0);
  REQUIRE(streamer.unload_count() > 0);
  REQUIRE(streamer.load_count() > 0);
  REQUIRE(streamer.resident_count() == 9);
  const engine::terrain::ChunkKey want_on{2, 0};
  const engine::terrain::ChunkKey want_off{0, 0};
  REQUIRE(streamer.resident().count(want_on) == 1);
  REQUIRE(streamer.resident().count(want_off) == 0);
}

TEST_CASE("ProbeVolume irradiance atlas CPU sample", "[m32][w9][gi]") {
  engine::gi::ProbeVolume vol;
  vol.Configure({0, 0, 0}, {1, 1, 1}, 2, 2, 2);
  const auto atlas = vol.BuildIrradianceAtlasCpu();
  REQUIRE(atlas.size() == 2 * 2 * 2 * 3);

  const auto direct = vol.Sample({0.5f, 0.5f, 0.5f});
  const auto from_atlas = vol.SampleAtlasCpu(atlas, {0.5f, 0.5f, 0.5f});
  REQUIRE(std::fabs(direct.r - from_atlas.r) < 1e-4f);
  REQUIRE(std::fabs(direct.g - from_atlas.g) < 1e-4f);
  REQUIRE(std::fabs(direct.b - from_atlas.b) < 1e-4f);
}

TEST_CASE("LightFunction factor and LocalLight id field", "[m32][w9][c03]") {
  REQUIRE(engine::render::EvalLightFunctionFactor(engine::render::LightFunctionProfile::Off,
                                                  0.5f) == 1.f);
  const float soft0 =
      engine::render::EvalLightFunctionFactor(engine::render::LightFunctionProfile::SoftDisk, 0.f);
  const float soft1 =
      engine::render::EvalLightFunctionFactor(engine::render::LightFunctionProfile::SoftDisk, 1.f);
  REQUIRE(soft0 > soft1);
  REQUIRE(soft0 <= 1.f + 1e-5f);
  REQUIRE(soft1 >= 0.f);

  const float radial = engine::render::EvalLightFunctionFactor(
      engine::render::LightFunctionProfile::RadialFalloff, 0.5f);
  REQUIRE(radial > 0.7f);
  REQUIRE(radial < 0.8f);

  REQUIRE(engine::render::ParseLightFunctionId("soft_disk") ==
          engine::render::LightFunctionProfile::SoftDisk);

  engine::render::LocalLight light;
  light.light_function_id = "radial";
  const auto profile = engine::render::ParseLightFunctionId(light.light_function_id);
  REQUIRE(engine::render::EvalLightFunctionFactor(profile, 0.f) == 1.f);
}

TEST_CASE("Path2D TessellateFillFan and EarClipSimple", "[m32][w9][g13]") {
  engine::render2d::Path2D path;
  path.MoveTo({0.f, 0.f});
  path.LineTo({4.f, 0.f});
  path.LineTo({4.f, 3.f});
  path.LineTo({0.f, 3.f});

  const auto fan = path.TessellateFillFan();
  REQUIRE(fan.topology == engine::render2d::Path2DTopology::TriangleList);
  REQUIRE(fan.vertices.size() == 4);
  REQUIRE(fan.indices.size() == 6);  // 2 tris

  const auto ears = path.EarClipSimple();
  REQUIRE(ears.topology == engine::render2d::Path2DTopology::TriangleList);
  REQUIRE(ears.vertices.size() == 4);
  REQUIRE(ears.indices.size() == 6);
}
