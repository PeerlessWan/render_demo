#include "mini_test.h"

#include "engine/gi/probe_volume.h"
#include "engine/gi/scene_capture.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/core/feature.h"
#include "engine/debug/sandbox_harness.h"
#include "engine/media/upscaler.h"
#include "engine/render/camera.h"
#include "engine/render/occlusion.h"
#include "engine/render2d/atlas.h"

#include <cmath>

TEST_CASE("Occlusion IsVisible uses frustum placeholder", "[wave][occlusion]") {
  engine::render::OcclusionBuffer occ;
  occ.Configure(256, 256);
  engine::render::Camera cam;
  cam.position = {0, 0, 5};
  const auto vp = cam.view_proj_matrix(1.f);
  engine::Aabb visible{{-0.5f, -0.5f, -1.f}, {0.5f, 0.5f, 0.f}};
  engine::Aabb hidden{{ -0.5f, -0.5f, 10.f}, {0.5f, 0.5f, 11.f}};
  REQUIRE(occ.IsVisible(visible, vp));
  REQUIRE_FALSE(occ.IsVisible(hidden, vp));
}

TEST_CASE("IndirectDrawArgs FillIndirectArgs", "[wave][gpu_driven]") {
  engine::gpu_driven::IndirectDrawArgs args{};
  engine::gpu_driven::FillIndirectArgs(args, 36, 4);
  REQUIRE(args.index_count_per_instance == 36);
  REQUIRE(args.instance_count == 4);
  REQUIRE(args.start_index_location == 0);
  REQUIRE(args.base_vertex_location == 0);
  REQUIRE(args.start_instance_location == 0);
}

TEST_CASE("ProbeVolume UpdateFromLights changes Sample", "[wave][gi]") {
  engine::gi::ProbeVolume vol;
  vol.Configure({0, 0, 0}, {2, 2, 2}, 2, 1, 1);
  const auto before = vol.Sample({0.1f, 0.1f, 0.1f});
  engine::gi::ProbeLight light;
  light.position = {0.1f, 0.1f, 0.1f};
  light.color = {1.f, 0.f, 0.f, 1.f};
  light.intensity = 8.f;
  light.range = 3.f;
  vol.UpdateFromLights({&light, 1});
  const auto after = vol.Sample({0.1f, 0.1f, 0.1f});
  REQUIRE(after.r > before.r);
}

TEST_CASE("LoadAtlasJson failure path", "[wave][render2d]") {
  std::vector<engine::render2d::AtlasFrame> frames;
  REQUIRE_FALSE(engine::render2d::LoadAtlasJson("{}", frames));
  REQUIRE(frames.empty());
  REQUIRE_FALSE(engine::render2d::LoadAtlasJson(R"({"frames":[)", frames));
  REQUIRE(frames.empty());
}

TEST_CASE("Upscaler create and pass-through", "[wave][media]") {
  auto upscaler = engine::media::CreateUpscaler();
  REQUIRE(upscaler);
  REQUIRE(upscaler->name() != nullptr);
  std::vector<std::uint8_t> src = {255, 0, 0, 255};
  std::vector<std::uint8_t> dst;
  REQUIRE(upscaler->Upscale(src, 1, 1, dst, 1, 1));
  REQUIRE(dst.size() == 4);
  REQUIRE(dst[0] == 255);
}

TEST_CASE("ProbeFaceViewProj produces finite matrix", "[wave][gi]") {
  const auto vp = engine::gi::ProbeFaceViewProj({0.f, 1.f, 0.f}, 0);
  REQUIRE(std::isfinite(vp.m[0]));
  REQUIRE(std::isfinite(vp.m[15]));
}

TEST_CASE("Feature overrides for gpu_instancing / execute_indirect", "[wave][feature]") {
  engine::ClearFeatureOverrides();
  REQUIRE_FALSE(engine::QueryFeature("gpu_instancing"));
  engine::SetFeatureOverride("gpu_instancing", true);
  REQUIRE(engine::QueryFeature("gpu_instancing"));
  engine::SetFeatureOverride("execute_indirect", true);
  REQUIRE(engine::QueryFeature("execute_indirect"));
  engine::ClearFeatureOverrides();
}

TEST_CASE("Sandbox harness parse ping", "[wave][harness]") {
  engine::debug::HarnessCommand cmd;
  std::string err;
  REQUIRE(engine::debug::ParseHarnessLine(R"({"cmd":"ping"})", cmd, err));
  REQUIRE(cmd.cmd == "ping");
  REQUIRE(engine::debug::HarnessOk().find("\"ok\":true") != std::string::npos);
}
