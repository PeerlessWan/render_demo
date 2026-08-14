#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/gi/reflection_probe.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

namespace {

void ParseHeadless(int argc, char** argv, engine::ApplicationDesc& desc) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      desc.headless_frames = std::atoi(argv[++i]);
    }
  }
}

engine::render::RenderSystemDesc LitDesc() {
  const auto dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  engine::render::RenderSystemDesc r{};
  r.lit_vs = dir / "lit_cube.vs.cso";
  r.lit_ps = dir / "lit_cube.ps.cso";
  r.shadow_vs = dir / "shadow.vs.cso";
  r.shadow_ps = dir / "shadow.ps.cso";
  r.quad_vs = dir / "quad.vs.cso";
  r.quad_ps = dir / "quad.ps.cso";
  r.post_vs = dir / "post_ssao_taa.vs.cso";
  r.post_ps = dir / "post_ssao_taa.ps.cso";
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
  r.quality.enable_ssr = true;
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 26 — P1 Post + Reflect";
  ParseHeadless(argc, argv, desc);
  if (desc.headless_frames <= 0) {
    desc.headless_frames = 2;
  }

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  auto cube = a.world().CreateNode("cube");
  {
    engine::scene::Transform t;
    t.position = {0.f, 0.5f, 0.f};
    a.world().set_local_transform(cube, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "metal";
    a.world().set_mesh(cube, mesh);
  }

  engine::gi::ReflectionProbe probe;
  probe.Configure({0.f, 1.5f, 0.f}, 32);
  probe.UpdateFromEnvironment({0.3f, -1.f, 0.2f}, {1.f, 0.96f, 0.9f, 1.f}, 2.5f,
                              {0.08f, 0.09f, 0.11f, 1.f});
  (void)a.device().UploadReflectionCubemap(probe.rgba_faces().data(), probe.face_size());

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }
  auto fx = render.effect_tuning();
  fx.enable_reflection_probe = true;
  fx.reflection_intensity = 0.55f;
  fx.enable_ssr = true;
  fx.enable_dof = true;
  fx.enable_motion_blur = true;
  fx.enable_fog = true;
  render.set_effect_tuning(fx);
  render.set_post_enabled("SSR", true);
  render.set_post_enabled("DoF", true);
  render.set_post_enabled("MotionBlur", true);
  render.set_post_enabled("VolumetricFog", true);
  engine::LogInfo("P1 FX: SSR DoF MotionBlur Fog + reflection probe");

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float dh = static_cast<float>(app_ref.window().height());
    const float aspect = dh > 0.f ? static_cast<float>(app_ref.window().width()) / dh : 1.f;
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect); !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
