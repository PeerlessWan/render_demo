#include "mini_test.h"

#include "engine/app/application.h"
#include "engine/gi/probe_volume.h"
#include "engine/gpu_driven/path.h"
#include "engine/mixed/pick.h"
#include "engine/render2d/tilemap_stream.h"
#include "engine/rt/raytracing.h"
#include "engine/rhi/i_device.h"
#include "engine/terrain/heightmap.h"

#include <cmath>

TEST_CASE("Headless device clear readback", "[headless]") {
  engine::rhi::DeviceDesc desc;
  desc.width = 32;
  desc.height = 16;
  desc.headless = true;
  auto device = engine::rhi::CreateHeadlessDevice(desc);
  REQUIRE(device);
  REQUIRE(device.value()->is_headless());
  REQUIRE(device.value()->BeginFrame());
  REQUIRE(device.value()->Clear({1.f, 0.f, 0.f, 1.f}));
  REQUIRE(device.value()->DispatchCompute({4, 4, 1}));
  REQUIRE_FALSE(device.value()->DispatchCompute({0, 1, 1}));
  std::vector<std::uint8_t> rgba;
  int w = 0, h = 0;
  REQUIRE(device.value()->ReadbackTextureStub(rgba, w, h));
  REQUIRE(w == 32);
  REQUIRE(h == 16);
  REQUIRE(rgba.size() == 32 * 16 * 4);
  REQUIRE(rgba[0] == 255);
  REQUIRE(rgba[1] == 0);
  REQUIRE(device.value()->Present());
}

TEST_CASE("Headless application runs N frames", "[headless]") {
  engine::ApplicationDesc desc;
  desc.headless = true;
  desc.headless_frames = 5;
  desc.window.width = 64;
  desc.window.height = 64;
  desc.clear_color = {0.2f, 0.3f, 0.4f, 1.f};

  auto app = engine::Application::Create(desc);
  REQUIRE(app);
  REQUIRE(app.value()->is_headless());

  int draws = 0;
  REQUIRE(app.value()->Run([&](engine::Application& a) {
    ++draws;
    REQUIRE(a.device().is_headless());
    (void)a.render_scene();
  }));
  REQUIRE(draws == 5);
  REQUIRE(app.value()->frame_index() == 5);
}

TEST_CASE("Mixed pick prefers sprite under cursor", "[m20]") {
  std::vector<engine::render::RenderInstance> none;
  std::vector<engine::render2d::Sprite> sprites(2);
  sprites[0].position = {0, 0};
  sprites[0].size = {10, 10};
  sprites[1].position = {5, 5};
  sprites[1].size = {10, 10};
  engine::mixed::PickQuery q;
  q.screen_px = {8, 8};
  q.viewport_w = 100;
  q.viewport_h = 100;
  const auto hit = engine::mixed::Pick(none, sprites, q);
  REQUIRE(hit.kind == engine::mixed::PickHit::Kind::Sprite2D);
  REQUIRE(hit.sprite_index == 1);
  REQUIRE(engine::mixed::IntegerScale(1920, 1080, 640, 360) == 3);
}

TEST_CASE("Tilemap streamer respects budget", "[m21]") {
  engine::render2d::TilemapStreamer stream;
  stream.Configure(64, 64, 16, 2);
  stream.SetGid(0, 0, 7);
  stream.UpdateResidence(0, 0, 10);
  REQUIRE(stream.resident_count() <= 2);
  REQUIRE(stream.FindChunk(0, 0) != nullptr);
}

TEST_CASE("Probe volume disable returns black", "[m22]") {
  engine::gi::ProbeVolume vol;
  vol.Configure({0, 0, 0}, {1, 1, 1}, 2, 2, 2);
  REQUIRE(vol.probes().size() == 8);
  const auto lit = vol.Sample({0.1f, 0.1f, 0.1f});
  REQUIRE(lit.r > 0.f);
  vol.set_enabled(false);
  const auto dark = vol.Sample({0.1f, 0.1f, 0.1f});
  REQUIRE(dark.r == 0.f);
}

TEST_CASE("Heightmap sample and vegetation", "[m23]") {
  engine::terrain::Heightmap map;
  map.width = 4;
  map.height = 4;
  map.cell = 1.f;
  map.samples = {0, 0, 0, 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0};
  REQUIRE(std::fabs(engine::terrain::SampleHeight(map, 1.5f, 1.5f) - 2.f) < 1e-3f);
  const auto veg = engine::terrain::ScatterVegetation(map, 0.5f, 1);
  REQUIRE_FALSE(veg.empty());
  REQUIRE(engine::terrain::SelectTerrainLod(5.f, {10.f, 40.f}) == 0);
}

TEST_CASE("GPU driven path selection", "[m24]") {
  engine::gpu_driven::GpuDrivenConfig cfg;
  cfg.enable_indirect = true;
  cfg.enable_mesh_shader = true;
  auto features = engine::QueryFeatures();
  features.level = engine::FeatureLevel::L1;
  REQUIRE(engine::gpu_driven::SelectPath(cfg, features) == engine::gpu_driven::Path::IndirectDraw);
  features.level = engine::FeatureLevel::L2;
  REQUIRE(engine::gpu_driven::SelectPath(cfg, features) == engine::gpu_driven::Path::MeshShader);
  REQUIRE(engine::gpu_driven::ValidateConfig(cfg));
}

TEST_CASE("Raytracing resolve fallback safe", "[m25]") {
  engine::rt::RaytracingConfig cfg;
  cfg.enable = true;
  cfg.allow_fallback = true;
  auto features = engine::QueryFeatures();
  features.raytracing = false;
  REQUIRE(engine::rt::Resolve(engine::rhi::Backend::Vulkan, features, cfg) ==
          engine::rt::RtStatus::UnsupportedFallback);
  REQUIRE(engine::rt::EnsureSafe(engine::rhi::Backend::Vulkan, features, cfg));
  cfg.allow_fallback = false;
  REQUIRE_FALSE(engine::rt::EnsureSafe(engine::rhi::Backend::Vulkan, features, cfg));
}
