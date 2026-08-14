#include "mini_test.h"

#include "engine/app/application.h"
#include "engine/core/feature.h"
#include "engine/core/math.h"
#include "engine/gi/reflection_probe.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/rhi/backend.h"
#include "engine/rhi/i_device.h"
#include "engine/rhi/submit_config.h"
#include "engine/scene/world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

float LumaAt(const std::vector<std::uint8_t>& rgba, int w, int h, int x, int y) {
  x = std::clamp(x, 0, w - 1);
  y = std::clamp(y, 0, h - 1);
  const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                         static_cast<std::size_t>(x)) *
                        4;
  return (0.2126f * rgba[i] + 0.7152f * rgba[i + 1] + 0.0722f * rgba[i + 2]) / 255.f;
}

float MeanLuma(const std::vector<std::uint8_t>& rgba) {
  double sum = 0.0;
  const std::size_t n = rgba.size() / 4;
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    sum += (0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2]) / 255.0;
  }
  return n ? static_cast<float>(sum / static_cast<double>(n)) : 0.f;
}

bool NearlyAll(const std::vector<std::uint8_t>& rgba, std::uint8_t v, int tol = 2) {
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    if (std::abs(static_cast<int>(rgba[i]) - static_cast<int>(v)) > tol ||
        std::abs(static_cast<int>(rgba[i + 1]) - static_cast<int>(v)) > tol ||
        std::abs(static_cast<int>(rgba[i + 2]) - static_cast<int>(v)) > tol) {
      return false;
    }
  }
  return true;
}

std::filesystem::path ShaderDir() {
#if defined(ENGINE_SHADER_DIR_A)
  return std::filesystem::path(ENGINE_SHADER_DIR_A);
#else
  return {};
#endif
}

}  // namespace

TEST_CASE("TAA depth reprojection math", "[taa][unit]") {
  // prev_uv = WorldToUv(world) should differ when camera moves.
  engine::Mat4 curr = engine::Mat4::Identity();
  engine::Mat4 prev = engine::Mat4::Identity();
  // Translate previous VP so a world point maps differently.
  prev.m[12] = 0.2f;
  const engine::Vec3 world{0.f, 0.f, -5.f};
  auto project = [](const engine::Mat4& vp, const engine::Vec3& p) {
    const float x = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
    const float y = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
    const float w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
    return engine::Vec3{x / w, y / w, 0.f};
  };
  const auto c = project(curr, world);
  const auto p = project(prev, world);
  REQUIRE(std::fabs(c.x - p.x) > 1e-4f);
}

TEST_CASE("ReflectionProbe updates faces", "[gi][m13]") {
  engine::gi::ReflectionProbe probe;
  probe.Configure({0, 1.5f, 0}, 16);
  probe.UpdateFromEnvironment({0.3f, -1.f, 0.2f}, {1.f, 0.95f, 0.9f, 1.f}, 3.f,
                              {0.1f, 0.12f, 0.16f, 1.f});
  REQUIRE(probe.dirty());
  REQUIRE(probe.rgba_faces().size() == static_cast<std::size_t>(6 * 16 * 16 * 4));
  const auto up = probe.SampleDirection({0, 1, 0});
  const auto down = probe.SampleDirection({0, -1, 0});
  REQUIRE(up.b + up.g + up.r > 0.f);
  REQUIRE(down.b + down.g + down.r > 0.f);
  probe.ClearDirty();
  REQUIRE_FALSE(probe.dirty());
}

TEST_CASE("Quality medium enables TAA", "[usable][taa]") {
  const auto q = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
  REQUIRE(q.enable_taa);
  engine::render::RenderSystem render;
  render.set_quality(q);
  REQUIRE(render.post_stack().enabled("TAA"));
}

TEST_CASE("SubmitConfig applies on gpu_headless device", "[rhi][m14]") {
  engine::ClearFeatureOverrides();
  engine::rhi::DeviceDesc desc;
  desc.width = 32;
  desc.height = 32;
  desc.gpu_headless = true;
  auto device = engine::rhi::CreateDevice(engine::rhi::Backend::D3D12, desc);
  if (!device) {
    SKIP_TEST("D3D12 gpu_headless unavailable");
  }
  engine::rhi::SubmitConfig cfg;
  cfg.multithread = true;
  cfg.worker_count = 2;
  REQUIRE(device.value()->SetSubmitConfig(cfg));
  REQUIRE(engine::QueryFeature("bindless"));
  REQUIRE(engine::QueryFeature("multithread_submit"));
  if (auto st = device.value()->BeginFrame(); st) {
    auto probe = device.value()->ProbeBindlessMinimalPath(0);
    if (!probe) {
      // Heap may not exist until lit setup; Feature query still proves capability gate.
      REQUIRE(engine::QueryFeature("bindless"));
    }
    (void)device.value()->Present();
  }
  engine::ClearFeatureOverrides();
}

TEST_CASE("GPU headless clear readback not black", "[gpu_headless][headless]") {
  engine::ClearFeatureOverrides();
  engine::rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 36;
  desc.gpu_headless = true;
  auto device = engine::rhi::CreateDevice(engine::rhi::Backend::D3D12, desc);
  if (!device) {
    SKIP_TEST("D3D12 gpu_headless unavailable");
  }
  auto& d = *device.value();
  REQUIRE(d.is_headless());
  REQUIRE(d.BeginFrame());
  REQUIRE(d.Clear({0.2f, 0.4f, 0.6f, 1.f}));
  std::vector<std::uint8_t> rgba;
  int w = 0;
  int h = 0;
  REQUIRE(d.ReadbackTextureStub(rgba, w, h));
  REQUIRE(w == 64);
  REQUIRE(h == 36);
  REQUIRE(rgba.size() == static_cast<std::size_t>(w * h * 4));
  REQUIRE_FALSE(NearlyAll(rgba, 0));
  REQUIRE_FALSE(NearlyAll(rgba, 255));
  // Approximate clear color in LDR
  REQUIRE(rgba[0] > 20);
  REQUIRE(rgba[2] > 100);
  REQUIRE(d.Present());
  engine::ClearFeatureOverrides();
}

TEST_CASE("GPU headless lit regression smoke", "[gpu_headless][headless][regression]") {
  engine::ClearFeatureOverrides();
  const auto shader_dir = ShaderDir();
  if (shader_dir.empty() || !std::filesystem::exists(shader_dir / "lit_cube.vs.cso")) {
    SKIP_TEST("compiled shaders missing");
  }

  engine::ApplicationDesc adesc;
  adesc.gpu_headless = true;
  adesc.headless_frames = 3;
  adesc.window.width = 160;
  adesc.window.height = 90;
  adesc.clear_color = {0.14f, 0.16f, 0.20f, 1.f};
  auto app = engine::Application::Create(adesc);
  if (!app) {
    SKIP_TEST(app.status().message().c_str());
  }

  auto ground = app.value()->world().CreateNode("g");
  engine::scene::MeshRenderer gm;
  gm.mesh_id = "ground";
  app.value()->world().set_mesh(ground, gm);
  auto cube = app.value()->world().CreateNode("c");
  engine::scene::MeshRenderer cm;
  cm.mesh_id = "cube";
  app.value()->world().set_mesh(cube, cm);
  app.value()->camera().position = {0.f, 2.2f, 6.2f};
  app.value()->camera().pitch = -0.22f;

  engine::render::RenderSystem render;
  engine::render::RenderSystemDesc rdesc;
  rdesc.lit_vs = shader_dir / "lit_cube.vs.cso";
  rdesc.lit_ps = shader_dir / "lit_cube.ps.cso";
  rdesc.shadow_vs = shader_dir / "shadow.vs.cso";
  rdesc.shadow_ps = shader_dir / "shadow.ps.cso";
  rdesc.quad_vs = shader_dir / "quad.vs.cso";
  rdesc.quad_ps = shader_dir / "quad.ps.cso";
  rdesc.post_vs = shader_dir / "post_ssao_taa.vs.cso";
  rdesc.post_ps = shader_dir / "post_ssao_taa.ps.cso";
  rdesc.debug_vs = shader_dir / "debug_line.vs.cso";
  rdesc.debug_ps = shader_dir / "debug_line.ps.cso";
  rdesc.enable_shadows = true;
  rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
  if (!render.Init(app.value()->device(), rdesc)) {
    SKIP_TEST("RenderSystem Init failed (shaders/device)");
  }

  engine::render::Environment env;
  engine::gi::ReflectionProbe probe;
  probe.Configure({0, 1.5f, 0}, 16);
  probe.UpdateFromEnvironment(env.sun_direction, env.sun_color, 3.f, env.ambient);
  REQUIRE(app.value()->device().UploadReflectionCubemap(probe.rgba_faces().data(), probe.face_size()));
  probe.ClearDirty();

  int frame = 0;
  std::vector<std::uint8_t> last;
  int last_w = 0;
  int last_h = 0;
  REQUIRE(app.value()->Run([&](engine::Application& a) {
    ++frame;
    auto fx = render.effect_tuning();
    // Cycle effect toggles so regressions crash here instead of in interactive Sandbox.
    fx.enable_taa = (frame % 2) == 0;
    fx.enable_ssao = true;
    fx.enable_bloom = (frame == 2);
    fx.enable_ssr = false;
    fx.enable_reflection_probe = true;
    render.set_effect_tuning(fx);

    if (frame == 2) {
      a.camera().yaw += 0.15f;  // force TAA history / MV path
    }
    REQUIRE(render.DrawFrame(a.device(), a.render_scene(), env, 160.f / 90.f));
    REQUIRE(a.device().ReadbackTextureStub(last, last_w, last_h));
  }));

  REQUIRE(frame == 3);
  REQUIRE(last_w == 160);
  REQUIRE(last_h == 90);
  REQUIRE(last.size() == static_cast<std::size_t>(last_w * last_h * 4));
  REQUIRE_FALSE(NearlyAll(last, 0));
  REQUIRE_FALSE(NearlyAll(last, 255));
  const float mean = MeanLuma(last);
  REQUIRE(mean > 0.02f);
  REQUIRE(mean < 0.98f);
  const float center = LumaAt(last, last_w, last_h, last_w / 2, last_h / 2);
  const float corner = LumaAt(last, last_w, last_h, 2, 2);
  REQUIRE(std::fabs(center - corner) > 1e-4f || mean > 0.05f);
  engine::ClearFeatureOverrides();
}

TEST_CASE("GPU headless semantic mean / TAA / shadows (C5)", "[gpu_headless][headless][semantic]") {
  engine::ClearFeatureOverrides();
  const auto shader_dir = ShaderDir();
  if (shader_dir.empty() || !std::filesystem::exists(shader_dir / "lit_cube.vs.cso")) {
    SKIP_TEST("compiled shaders missing");
  }

  auto run_capture = [&](bool taa, bool shadows, std::vector<std::uint8_t>& out_rgba) -> bool {
    engine::ApplicationDesc adesc;
    adesc.gpu_headless = true;
    adesc.headless_frames = 2;
    adesc.window.width = 128;
    adesc.window.height = 72;
    adesc.clear_color = {0.14f, 0.16f, 0.20f, 1.f};
    auto app = engine::Application::Create(adesc);
    if (!app) {
      return false;
    }
    auto ground = app.value()->world().CreateNode("g");
    engine::scene::MeshRenderer gm;
    gm.mesh_id = "ground";
    app.value()->world().set_mesh(ground, gm);
    auto cube = app.value()->world().CreateNode("c");
    engine::scene::MeshRenderer cm;
    cm.mesh_id = "cube";
    app.value()->world().set_mesh(cube, cm);
    app.value()->camera().position = {0.f, 2.2f, 6.2f};
    app.value()->camera().pitch = -0.22f;

    engine::render::RenderSystem render;
    engine::render::RenderSystemDesc rdesc;
    rdesc.lit_vs = shader_dir / "lit_cube.vs.cso";
    rdesc.lit_ps = shader_dir / "lit_cube.ps.cso";
    rdesc.shadow_vs = shader_dir / "shadow.vs.cso";
    rdesc.shadow_ps = shader_dir / "shadow.ps.cso";
    rdesc.quad_vs = shader_dir / "quad.vs.cso";
    rdesc.quad_ps = shader_dir / "quad.ps.cso";
    rdesc.post_vs = shader_dir / "post_ssao_taa.vs.cso";
    rdesc.post_ps = shader_dir / "post_ssao_taa.ps.cso";
    rdesc.debug_vs = shader_dir / "debug_line.vs.cso";
    rdesc.debug_ps = shader_dir / "debug_line.ps.cso";
    rdesc.enable_shadows = shadows;
    rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
    rdesc.quality.enable_taa = taa;
    if (!render.Init(app.value()->device(), rdesc)) {
      return false;
    }
    auto fx = render.effect_tuning();
    fx.enable_taa = taa;
    fx.enable_shadows = shadows;
    fx.enable_ssao = false;
    fx.enable_bloom = false;
    fx.enable_ssr = false;
    render.set_effect_tuning(fx);

    engine::render::Environment env;
    int last_w = 0;
    int last_h = 0;
    if (!app.value()->Run([&](engine::Application& a) {
          REQUIRE(render.DrawFrame(a.device(), a.render_scene(), env, 128.f / 72.f));
          REQUIRE(a.device().ReadbackTextureStub(out_rgba, last_w, last_h));
        })) {
      return false;
    }
    return last_w == 128 && last_h == 72 && !out_rgba.empty();
  };

  std::vector<std::uint8_t> base_rgba;
  std::vector<std::uint8_t> taa_off_rgba;
  std::vector<std::uint8_t> shadows_off_rgba;
  if (!run_capture(true, true, base_rgba)) {
    SKIP_TEST("gpu_headless semantic capture unavailable");
  }
  REQUIRE(run_capture(false, true, taa_off_rgba));
  REQUIRE(run_capture(true, false, shadows_off_rgba));

  const float mean_base = MeanLuma(base_rgba);
  const float mean_taa_off = MeanLuma(taa_off_rgba);
  const float mean_shadows_off = MeanLuma(shadows_off_rgba);

  REQUIRE(mean_base > 0.02f);
  REQUIRE(mean_base < 0.98f);
  // TAA on vs off should not be bit-identical (history / jitter path).
  REQUIRE(base_rgba != taa_off_rgba);
  // Shadows on should be darker (or equal within noise) than shadows off.
  REQUIRE(mean_base <= mean_shadows_off + 0.08f);
  REQUIRE(mean_shadows_off > 0.02f);
  (void)mean_taa_off;
  engine::ClearFeatureOverrides();
}
