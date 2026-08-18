#include "engine/app/application.h"
#include "engine/core/log.h"
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

engine::render::RenderSystemDesc LitDesc(engine::render::QualityTier tier) {
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
  r.quality = engine::render::QualitySettings::FromTier(tier);
  return r;
}

const char* TierName(engine::render::QualityTier t) {
  switch (t) {
    case engine::render::QualityTier::Low:
      return "Low";
    case engine::render::QualityTier::Medium:
      return "Medium";
    case engine::render::QualityTier::High:
      return "High";
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 13 — Environment + Quality";
  ParseHeadless(argc, argv, desc);
  if (desc.headless_frames <= 0) {
    desc.headless_frames = 2;
  }

  for (const auto tier : {engine::render::QualityTier::Low, engine::render::QualityTier::Medium,
                          engine::render::QualityTier::High}) {
    const auto q = engine::render::QualitySettings::FromTier(tier);
    engine::LogInfo(std::string("QualityTier=") + TierName(tier) +
                    " cascades=" + std::to_string(q.shadow_cascades) +
                    " veg_cap=" + std::to_string(q.vegetation_cap) +
                    " bloom=" + (q.enable_bloom ? "1" : "0") +
                    " ssao=" + (q.enable_ssao ? "1" : "0"));
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
    mesh.mesh_id = "cube";
    a.world().set_mesh(cube, mesh);
  }

  engine::render::Environment env;
  env.fog_enabled = true;
  env.fog_density = 0.035f;
  env.fog_start = 4.f;
  env.fog_color = {0.55f, 0.65f, 0.78f, 1.f};
  env.exposure = 1.1f;
  env.enable_atmosphere = false;
  engine::LogInfo("Environment fog_enabled=1 density=" + std::to_string(env.fog_density));

  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc(engine::render::QualityTier::Medium)); !st) {
    engine::LogError(st.message());
    return 1;
  }
  render.ApplyEnvironmentDefaults(env);

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float dh = static_cast<float>(app_ref.window().height());
    const float aspect = dh > 0.f ? static_cast<float>(app_ref.window().width()) / dh : 1.f;
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect); !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
