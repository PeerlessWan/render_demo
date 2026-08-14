#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"

#include <cstdlib>
#include <cstring>
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
  r.debug_vs = dir / "debug_line.vs.cso";
  r.debug_ps = dir / "debug_line.ps.cso";
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  r.quality.enable_ssao = false;
  r.quality.enable_taa = false;
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 03 — Texture + Depth";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.08f, 0.10f, 0.14f, 1.f};
  ParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  a.camera().position = {0.f, 2.2f, 5.5f};
  a.camera().pitch = -0.28f;

  auto ground = a.world().CreateNode("ground");
  {
    engine::scene::Transform t;
    t.scale = {6.f, 1.f, 6.f};
    a.world().set_local_transform(ground, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "ground";
    mesh.never_cull = true;
    mesh.local_bounds = {{-6.f, -0.05f, -6.f}, {6.f, 0.05f, 6.f}};
    a.world().set_mesh(ground, mesh);
  }
  for (int i = 0; i < 2; ++i) {
    auto id = a.world().CreateNode("cube" + std::to_string(i));
    engine::scene::Transform t;
    t.position = {static_cast<float>(i) * 1.4f - 0.7f, 0.5f, static_cast<float>(i) * -1.2f};
    a.world().set_local_transform(id, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(id, mesh);
  }

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float dh = static_cast<float>(app_ref.window().height());
    const float aspect = dh > 0.f ? static_cast<float>(app_ref.window().width()) / dh : 1.f;
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect); !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
