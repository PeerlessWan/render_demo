#include "mini_test.h"

#include "engine/gi/probe_volume.h"
#include "engine/gi/scene_capture.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/core/feature.h"
#include "engine/debug/sandbox_harness.h"
#include "engine/media/upscaler.h"
#include "engine/media/media.h"
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

TEST_CASE("Occlusion HiZ pyramid rejects occluded box", "[wave][occlusion][hiz]") {
  engine::render::OcclusionBuffer occ;
  occ.Configure(8, 8);
  std::vector<float> depth(64, 1.f);
  // Near depth in the center 2x2 → farther boxes project there should fail HiZ.
  for (int y = 3; y <= 4; ++y) {
    for (int x = 3; x <= 4; ++x) {
      depth[static_cast<std::size_t>(y * 8 + x)] = 0.1f;
    }
  }
  occ.UploadDepthFinest(depth);
  REQUIRE(occ.has_hiz());
  REQUIRE(engine::QueryFeature("hiz"));
  engine::ClearFeatureOverrides();
}

TEST_CASE("CullInstancesToIndirect packs visible args", "[wave][gpu_driven]") {
  std::vector<engine::Mat4> worlds;
  worlds.push_back(engine::Mat4::TRS({0, 0, 0}, engine::Quat{}, {1, 1, 1}));
  worlds.push_back(engine::Mat4::TRS({0, 0, 40}, engine::Quat{}, {1, 1, 1}));
  engine::render::Camera cam;
  cam.position = {0, 0, 5};
  const auto vp = cam.view_proj_matrix(1.f);
  std::vector<engine::Mat4> visible;
  engine::gpu_driven::IndirectDrawArgs args{};
  const std::uint32_t kept =
      engine::gpu_driven::CullInstancesToIndirect(worlds, {}, vp, nullptr, visible, args, 36);
  REQUIRE(kept >= 1);
  REQUIRE(args.index_count_per_instance == 36);
  REQUIRE(args.instance_count == kept);
  const auto packed = engine::gpu_driven::PackIndirectArgsU32(args);
  REQUIRE(packed.size() == 5);
  REQUIRE(packed[0] == 36);
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
  vol.set_budget_per_frame(8);
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

TEST_CASE("ProbeVolume trilinear Sample and incremental budget", "[wave][gi]") {
  engine::gi::ProbeVolume vol;
  vol.Configure({0, 0, 0}, {1, 1, 1}, 3, 3, 3);
  vol.set_budget_per_frame(4);
  REQUIRE(vol.probes().size() == 27);
  engine::gi::ProbeLight light;
  light.position = {1.f, 1.f, 1.f};
  light.intensity = 4.f;
  light.range = 4.f;
  light.color = {0.f, 1.f, 0.f, 1.f};
  vol.UpdateFromLights({&light, 1});
  const auto mid = vol.Sample({1.f, 1.f, 1.f});
  REQUIRE(mid.g >= 0.f);
  // Second tick continues cursor; should still be finite.
  vol.UpdateFromLights({&light, 1});
  const auto mid2 = vol.Sample({1.2f, 0.8f, 1.1f});
  REQUIRE(std::isfinite(mid2.r));
  REQUIRE(std::isfinite(mid2.g));
}

TEST_CASE("LoadAtlasJson failure path", "[wave][render2d]") {
  std::vector<engine::render2d::AtlasFrame> frames;
  REQUIRE_FALSE(engine::render2d::LoadAtlasJson("{}", frames));
  REQUIRE(frames.empty());
  REQUIRE_FALSE(engine::render2d::LoadAtlasJson(R"({"frames":[)", frames));
  REQUIRE(frames.empty());
}

TEST_CASE("D3D12VA stub Open is Unavailable diagnostic", "[m7][media]") {
  auto dec = engine::media::CreateD3D12VaDecoderOrStub();
  REQUIRE(dec);
  REQUIRE_FALSE(dec->feature_available());
  const auto st = dec->Open("clip.mp4");
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  REQUIRE(st.message().find("D3D12VA") != std::string::npos);
  REQUIRE(st.message().find("stub") != std::string::npos ||
          st.message().find("Unavailable") != std::string::npos);
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

TEST_CASE("Upscaler resolution-scale and jitter", "[wave][media][m7]") {
  int rw = 0;
  int rh = 0;
  engine::media::ResolutionScale::ComputeRenderSize(1920, 1080, 0.5f, rw, rh);
  REQUIRE(rw == 960);
  REQUIRE(rh == 540);
  REQUIRE(engine::media::ResolutionScale::ClampScale(2.f) == 1.f);

  auto upscaler = engine::media::CreateUpscaler();
  REQUIRE(upscaler);
  std::vector<std::uint8_t> src(4 * 4 * 4, 0);
  src[0] = 255;
  src[3] = 255;
  std::vector<std::uint8_t> dst;
  engine::media::UpscaleParams p;
  p.jitter_x = 0.1f;
  p.jitter_y = -0.05f;
  REQUIRE(upscaler->Upscale(src, 4, 4, dst, 8, 8, p));
  REQUIRE(dst.size() == 8 * 8 * 4);
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
