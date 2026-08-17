#include "mini_test.h"

#include "engine/app/application.h"
#include "engine/core/feature.h"
#include "engine/gi/lightmap.h"
#include "engine/gi/probe_volume.h"
#include "engine/gpu_driven/path.h"
#include "engine/mixed/pick.h"
#include "engine/render2d/bmfont.h"
#include "engine/render2d/fx2d.h"
#include "engine/render2d/sprite.h"
#include "engine/render2d/tilemap_stream.h"
#include "engine/rt/raytracing.h"
#include "engine/rhi/backend.h"
#include "engine/rhi/i_device.h"
#include "engine/terrain/heightmap.h"
#include "engine/vfx/particles.h"
#include "engine/vfx/trail_ribbon.h"
#include "engine/debug/debug_draw.h"

#include <cmath>
#include <filesystem>
#include <fstream>

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

// Smoke: Vulkan factory must not open a real Vk device in headless/unit CI.
TEST_CASE("CreateDevice Vulkan headless uses headless device", "[headless][vulkan]") {
  engine::rhi::DeviceDesc desc;
  desc.width = 16;
  desc.height = 16;
  desc.headless = true;
  auto device = engine::rhi::CreateDevice(engine::rhi::Backend::Vulkan, desc);
  REQUIRE(device);
  REQUIRE(device.value()->is_headless());
#if defined(ENGINE_WITH_VULKAN) && ENGINE_WITH_VULKAN
  REQUIRE(engine::QueryFeature("vulkan"));
#endif
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

TEST_CASE("Tilemap streamer expands resident chunks to sprites", "[m21]") {
  engine::render2d::TilemapStreamer stream;
  stream.Configure(32, 32, 16, 4);
  stream.SetGid(1, 2, 5);
  stream.SetGid(17, 2, 9);
  stream.UpdateResidence(0, 0, 0);  // only chunk (0,0)
  REQUIRE(stream.FindChunk(0, 0)->resident);
  REQUIRE_FALSE(stream.FindChunk(1, 0)->resident);

  std::vector<engine::render2d::Sprite> sprites;
  engine::render2d::TileExpandDesc desc;
  desc.tile_w = 16.f;
  desc.tile_h = 16.f;
  stream.ExpandResidentToSprites(sprites, desc);
  REQUIRE(sprites.size() == 1);
  REQUIRE(sprites[0].frame == 5);
  REQUIRE(std::fabs(sprites[0].position.x - 16.f) < 1e-3f);
  REQUIRE(std::fabs(sprites[0].position.y - 32.f) < 1e-3f);
}

TEST_CASE("Skeleton2D clip advances with time", "[m21]") {
  engine::render2d::Skeleton2D skel;
  skel.bones.push_back({"root", -1, {0.f, 0.f}, 0.f});
  skel.bones.push_back({"leg", 0, {0.f, -10.f}, 0.f});
  const auto clip = engine::render2d::MakeTinyWalkClip2D();

  float t = 0.f;
  engine::render2d::BonePose2D first{};
  engine::render2d::BonePose2D mid{};
  for (int i = 0; i < 8; ++i) {
    const auto pose = engine::render2d::SampleSkeletonClip2D(skel, clip, t);
    REQUIRE(pose.rotations.size() == 2);
    if (i == 0) {
      first = pose;
    }
    if (i == 4) {
      mid = pose;
    }
    t += 0.125f;
  }
  REQUIRE(std::fabs(mid.rotations[1] - first.rotations[1]) > 0.05f);
  // Loop: t=1.0 samples same as t=0.
  const auto looped = engine::render2d::SampleSkeletonClip2D(skel, clip, 1.f);
  REQUIRE(std::fabs(looped.rotations[1] - first.rotations[1]) < 1e-3f);
}

TEST_CASE("Fog tint and camera shake helpers", "[m21]") {
  std::vector<engine::render2d::Sprite> sprites(1);
  sprites[0].color = {1.f, 0.f, 0.f, 1.f};
  engine::render2d::ApplyFogTint2D(sprites, {0.f, 0.f, 1.f, 1.f}, 0.5f);
  REQUIRE(std::fabs(sprites[0].color.r - 0.5f) < 1e-3f);
  REQUIRE(std::fabs(sprites[0].color.b - 0.5f) < 1e-3f);

  engine::render2d::CameraShake2D shake;
  shake.max_offset = 10.f;
  shake.AddTrauma(1.f);
  shake.Step(0.016f);
  const auto off = shake.Offset();
  REQUIRE((std::fabs(off.x) + std::fabs(off.y)) > 0.f);
}

TEST_CASE("BMFont JSON stub loader", "[m21]") {
  const char* json = R"({
    "pages": ["tiny.png"],
    "lineHeight": 12,
    "glyphs": [
      {"ch": "A", "x": 0, "y": 0, "w": 8, "h": 10, "xadv": 9},
      {"ch": "B", "x": 8, "y": 0, "w": 7, "h": 10, "xadv": 8}
    ]
  })";
  engine::render2d::BmFontAtlas atlas;
  REQUIRE(engine::render2d::LoadBmFontJson(json, atlas));
  REQUIRE(atlas.line_height == 12);
  REQUIRE(atlas.pages.size() == 1);
  REQUIRE(atlas.glyphs.size() == 2);
  REQUIRE(atlas.glyphs.at(U'A').w == 8);
  REQUIRE(atlas.glyphs.at(U'B').xadvance == 8);

  engine::render2d::BmFontAtlas from_file;
  const auto path =
      std::filesystem::path(ENGINE_CONTENT_DIR_A) / "fonts" / "tiny_bmfont.json";
  REQUIRE(engine::render2d::LoadBmFontJsonFile(path, from_file));
  REQUIRE(from_file.glyphs.count(U'A') == 1);
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

TEST_CASE("LoadTiledJson multi-layer collision export", "[m16]") {
  const auto path = std::filesystem::temp_directory_path() / "engine_tiny_tiled_m16.json";
  {
    std::ofstream out(path);
    out << R"({
  "width": 2, "height": 2, "tilewidth": 16, "tileheight": 16,
  "tilesets": [{"firstgid": 1, "image": "tiles.png", "name": "t"}],
  "layers": [
    {"name": "ground", "type": "tilelayer", "width": 2, "height": 2, "data": [1, 1, 2, 2]},
    {"name": "collision", "type": "tilelayer", "width": 2, "height": 2, "data": [0, 1, 1, 0]}
  ]
})";
  }
  std::vector<engine::render2d::TilemapLayer> layers;
  REQUIRE(engine::render2d::LoadTiledJson(path, layers));
  REQUIRE(layers.size() == 2);
  REQUIRE(layers[0].tileset_image == "tiles.png");
  REQUIRE_FALSE(layers[0].collision);
  REQUIRE(layers[1].collision);
  std::vector<int> gids;
  int w = 0;
  int h = 0;
  REQUIRE(engine::render2d::ExportCollisionGids(layers, gids, w, h));
  REQUIRE(w == 2);
  REQUIRE(h == 2);
  REQUIRE(gids.size() == 4);
  REQUIRE(gids[1] == 1);
  std::filesystem::remove(path);
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
  const auto mesh = engine::terrain::BuildTerrainMesh(map, {0, 0, 0});
  REQUIRE(mesh.positions.size() == 4 * 4 * 3);
  REQUIRE(mesh.indices.size() == 3 * 3 * 6);
  const auto water = engine::terrain::BuildWaterPatchMesh(4.f);
  REQUIRE(water.positions.size() == 12);
  REQUIRE(water.indices.size() == 6);
}

TEST_CASE("CPU particle emitter steps and expires", "[m7][vfx]") {
  engine::vfx::ParticleEmitter em;
  em.Configure({0, 1, 0}, 0.f, 0.5f);
  em.EmitBurst(8);
  REQUIRE(em.particles().size() == 8);
  for (int i = 0; i < 20; ++i) {
    em.Step(0.1f);
  }
  REQUIRE(em.particles().empty());
}

TEST_CASE("TrailRibbon CPU update and segments", "[m7][vfx]") {
  engine::vfx::TrailRibbon trail;
  trail.Configure(0.5f, 0.1f, 8);
  trail.Push({0.f, 0.f, 0.f});
  trail.Push({1.f, 0.f, 0.f});
  trail.Push({2.f, 0.f, 0.f});
  REQUIRE(trail.points().size() == 3);
  auto segs = trail.BuildSegments();
  REQUIRE(segs.size() == 2);
  engine::debug::DebugDraw dd;
  trail.AppendDebugLines(dd);
  REQUIRE(dd.lines().size() == 2);
  trail.Step(1.f);
  REQUIRE(trail.points().empty());
}

TEST_CASE("GPU driven path selection", "[m24]") {
  engine::ClearFeatureOverrides();
  engine::gpu_driven::GpuDrivenConfig cfg;
  cfg.enable_indirect = true;
  cfg.enable_mesh_shader = true;
  auto features = engine::QueryFeatures();
  features.level = engine::FeatureLevel::L1;
  REQUIRE(engine::gpu_driven::SelectPath(cfg, features) == engine::gpu_driven::Path::IndirectDraw);
  features.level = engine::FeatureLevel::L2;
  // C08/M26: MeshShader is Feature SKIP unless override mesh_shader=true.
  REQUIRE(engine::gpu_driven::SelectPath(cfg, features) == engine::gpu_driven::Path::IndirectDraw);
  engine::SetFeatureOverride("mesh_shader", true);
  REQUIRE(engine::gpu_driven::SelectPath(cfg, features) == engine::gpu_driven::Path::MeshShader);
  engine::ClearFeatureOverrides();
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

TEST_CASE("CanRunDxrDemo requires raytracing feature", "[m25][m8]") {
  engine::rt::DxrDemoConfig demo;
  demo.enable_reflections = true;
  auto features = engine::QueryFeatures();
  features.d3d12 = true;
  features.raytracing = false;
  REQUIRE_FALSE(engine::rt::CanRunDxrDemo(features, demo));
  features.raytracing = true;
  REQUIRE(engine::rt::CanRunDxrDemo(features, demo));
  demo.enable_reflections = false;
  demo.enable_shadows = false;
  REQUIRE_FALSE(engine::rt::CanRunDxrDemo(features, demo));
}

TEST_CASE("DxrShadowDemo and RunDxrFullscreenStub contract", "[m25][w4][dxr]") {
  engine::ClearFeatureOverrides();
  engine::rt::DxrDemoConfig demo;
  demo.enable_shadows = true;
  auto features = engine::QueryFeatures();
  features.d3d12 = true;
  features.raytracing = false;
  REQUIRE_FALSE(engine::rt::DxrShadowDemo(features, demo).would_run);

  features.raytracing = true;
  REQUIRE(engine::rt::DxrShadowDemo(features, demo).would_run);
  demo.enable_shadows = false;
  REQUIRE_FALSE(engine::rt::DxrShadowDemo(features, demo).would_run);

  engine::rhi::DeviceDesc ddesc;
  ddesc.headless = true;
  ddesc.width = 16;
  ddesc.height = 16;
  auto device = engine::rhi::CreateHeadlessDevice(ddesc);
  REQUIRE(device);
  engine::SetFeatureOverride("raytracing", false);
  auto stub_off = engine::rt::RunDxrFullscreenStub(*device.value());
  REQUIRE_FALSE(stub_off);
  REQUIRE(stub_off.code() == engine::ErrorCode::Unavailable);

  engine::SetFeatureOverride("raytracing", true);
  auto stub_on = engine::rt::RunDxrFullscreenStub(*device.value());
  REQUIRE(stub_on);

  // W7: real AS path is Unavailable without DXR HW (or Ok with HW); must not crash.
  const auto as_path = engine::rt::TryBuildCubeBlasTlasAndDispatchRays();
  REQUIRE((as_path.ok() || as_path.code() == engine::ErrorCode::Unavailable));
  engine::ClearFeatureOverrides();
}

TEST_CASE("Lightmap load and sample from content", "[m8][gi]") {
  const auto path =
      std::filesystem::path(ENGINE_CONTENT_DIR_A) / "ibl" / "lightmap.rgba";
  engine::gi::LightmapImage img;
  REQUIRE(engine::gi::LoadLightmapRgba(path, img));
  REQUIRE(img.width == 64);
  REQUIRE(img.height == 64);
  REQUIRE(img.rgba.size() == 64ull * 64ull * 4ull);
  const auto c = engine::gi::SampleLightmap(img, 0.5f, 0.5f);
  REQUIRE(c.r > 0.2f);
  REQUIRE(c.a > 0.9f);
}
