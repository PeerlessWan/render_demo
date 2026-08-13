#include "engine/app/application.h"

#include "engine/assets/image_loader.h"
#include "engine/core/log.h"
#include "engine/debug/console.h"
#include "engine/media/media.h"
#include "engine/mixed/pick.h"
#include "engine/net/net_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/render2d/sprite.h"
#include "engine/ui/immediate_ui.h"
#include "engine/ui/rml_ui.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

int main(int argc, char** argv) {
  engine::ApplicationDesc desc;
  desc.window.title = "Sandbox — FX(F1) Profiler(F2) WASD/mouse Esc";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.05f, 0.07f, 0.1f, 1.f};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 3;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      desc.headless_frames = std::atoi(argv[++i]);
    }
  }

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  a.set_net(std::make_shared<engine::net::NetSystem>());
  a.camera().position = {0.f, 2.f, 6.f};

  auto ground = a.world().CreateNode("ground");
  {
    engine::scene::Transform t;
    t.position = {0, -0.5f, 0};
    t.scale = {8.f, 1.f, 8.f};
    a.world().set_local_transform(ground, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "ground";
    a.world().set_mesh(ground, mesh);
  }
  for (int i = 0; i < 4; ++i) {
    auto id = a.world().CreateNode("box" + std::to_string(i));
    engine::scene::Transform t;
    t.position = {static_cast<float>(i) * 1.5f - 2.f, 0.5f, 0.f};
    a.world().set_local_transform(id, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(id, mesh);
  }
  {
    auto glass = a.world().CreateNode("glass");
    engine::scene::Transform t;
    t.position = {0.f, 1.2f, 2.f};
    t.scale = {1.2f, 1.2f, 1.2f};
    a.world().set_local_transform(glass, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "glass";
    a.world().set_mesh(glass, mesh);
  }

  engine::render::Environment env;
  env.sun_direction = {0.35f, -1.f, 0.25f};
  env.sun_intensity = 2.8f;

  engine::render::RenderSystem render;
  engine::render::RenderSystemDesc rdesc;
  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  rdesc.lit_vs = shader_dir / "lit_cube.vs.cso";
  rdesc.lit_ps = shader_dir / "lit_cube.ps.cso";
  rdesc.shadow_vs = shader_dir / "shadow.vs.cso";
  rdesc.shadow_ps = shader_dir / "shadow.ps.cso";
  rdesc.quad_vs = shader_dir / "quad.vs.cso";
  rdesc.quad_ps = shader_dir / "quad.ps.cso";
  rdesc.post_vs = shader_dir / "post_ssao_taa.vs.cso";
  rdesc.post_ps = shader_dir / "post_ssao_taa.ps.cso";
  rdesc.enable_shadows = true;
  rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  if (auto st = render.Init(a.device(), rdesc); !st) {
    engine::LogError(st.message());
    return 1;
  }
  {
#ifndef ENGINE_CONTENT_DIR_A
#error "ENGINE_CONTENT_DIR_A must be set by CMake"
#endif
    const auto content = std::filesystem::path(ENGINE_CONTENT_DIR_A);
    auto loader = engine::assets::CreateDefaultImageLoader();
    if (auto alb = loader->LoadFile(content / "textures" / "albedo_brick.png")) {
      if (auto st = a.device().UploadLitAlbedoRgba(alb->rgba.data(), alb->width, alb->height); !st) {
        engine::LogError(st.message());
        return 1;
      }
      engine::LogInfo(std::string("Albedo from file (") + loader->backend_name() + ")");
    } else {
      engine::LogError(alb.status().message());
      return 1;
    }
    if (auto orm = loader->LoadFile(content / "textures" / "orm_brick.png")) {
      if (auto st = a.device().UploadLitOrmRgba(orm->rgba.data(), orm->width, orm->height); !st) {
        engine::LogError(st.message());
        return 1;
      }
      engine::LogInfo("ORM from file (R=AO G=rough B=metal)");
    } else {
      engine::LogError(orm.status().message());
      return 1;
    }
  }
  {
    std::vector<engine::render::LocalLight> lights;
    engine::render::LocalLight lamp;
    lamp.id = 1;
    lamp.position = {1.5f, 2.5f, 0.5f};
    lamp.range = 12.f;
    lamp.color = {1.f, 0.55f, 0.25f, 1.f};
    lamp.intensity = 6.f;
    lamp.shadow_resolution = 512;
    lamp.cast_shadows = true;
    lights.push_back(lamp);
    engine::render::LocalLight cool;
    cool.id = 2;
    cool.position = {-2.f, 2.2f, 1.5f};
    cool.range = 10.f;
    cool.color = {0.35f, 0.55f, 1.f, 1.f};
    cool.intensity = 4.5f;
    cool.shadow_resolution = 512;
    cool.cast_shadows = true;
    lights.push_back(cool);
    render.set_local_lights(lights);
  }

  engine::render::EffectTuning fx = render.effect_tuning();
  fx.sun_intensity = env.sun_intensity;
  fx.enable_ssao = rdesc.quality.enable_ssao;
  fx.enable_taa = rdesc.quality.enable_taa;
  fx.shadow_cascades = rdesc.quality.shadow_cascades;
  render.set_effect_tuning(fx);

  engine::ui::ImmediateUi imgui;
  if (!imgui.available()) {
    engine::LogError("Dear ImGui not available (ENGINE_WITH_IMGUI=0)");
    return 1;
  }
  {
    engine::ui::ImmediateUiDesc ui_desc;
    ui_desc.ui_vs = shader_dir / "ui_imgui.vs.cso";
    ui_desc.ui_ps = shader_dir / "ui_imgui.ps.cso";
    if (auto st = imgui.Init(a.device(), ui_desc); !st) {
      engine::LogError(st.message());
      return 1;
    }
  }

  auto physics = engine::physics::CreateDefaultPhysicsWorld();
  engine::LogInfo(std::string("Physics backend: ") + physics->backend_name());
  engine::LogInfo(std::string("Retained UI backend: ") +
                  engine::ui::QueryRetainedUiBackend().name);
  engine::physics::RigidBodyDesc falling;
  falling.position = {0, 4, -2};
  const int phys_id = physics->CreateBox(falling);
  auto phys_node = a.world().CreateNode("phys_box");
  {
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(phys_node, mesh);
  }

  bool panel_open = true;
  bool profiler_open = false;
  bool f1_was_down = false;
  bool f2_was_down = false;
  engine::debug::Profiler profiler;

  std::vector<engine::render2d::Sprite> sprites;

  auto audio = engine::media::CreateDefaultAudioDevice();
  engine::LogInfo(std::string("Audio backend: ") + audio->backend_name());
  engine::LogInfo("Sandbox: F1 Effects | F2 Profiler | WASD/mouse | Esc quit");

  const auto status = a.Run([&](engine::Application& app_ref) {
    profiler.Begin("Frame");
    physics->Step(app_ref.delta_time());
    {
      engine::scene::Transform t = app_ref.world().local_transform(phys_node);
      t.position = physics->body_position(phys_id);
      app_ref.world().set_local_transform(phys_node, t);
    }
    app_ref.world().UpdateTransforms();

    const auto& snap = app_ref.window().input_snapshot();
    const bool f1_down = snap.keys[VK_F1];
    if (f1_down && !f1_was_down) {
      panel_open = !panel_open;
    }
    f1_was_down = f1_down;
    const bool f2_down = snap.keys[VK_F2];
    if (f2_down && !f2_was_down) {
      profiler_open = !profiler_open;
    }
    f2_was_down = f2_down;

    const float dw = static_cast<float>(app_ref.window().width());
    const float dh = static_cast<float>(app_ref.window().height());
    imgui.BeginFrame(snap, dw, dh, app_ref.delta_time());

    if (panel_open) {
      if (imgui.BeginWindow("Effects", 16.f, 48.f, 340.f, 460.f)) {
        imgui.Text("WASD/QE | mouse | F1/F2 | Esc");
        imgui.Separator();
        imgui.Checkbox("Shadows", &fx.enable_shadows);
        imgui.Checkbox("SSAO", &fx.enable_ssao);
        imgui.Checkbox("TAA", &fx.enable_taa);
        imgui.Separator();
        imgui.SliderFloat("Sun intensity", &fx.sun_intensity, 0.f, 8.f);
        imgui.SliderFloat("Ambient scale", &fx.ambient_scale, 0.f, 3.f);
        imgui.SliderFloat("Shadow bias", &fx.shadow_bias, 0.0001f, 0.02f);
        imgui.SliderFloat("Specular power", &fx.specular_power, 1.f, 128.f);
        imgui.SliderFloat("Local light scale", &fx.local_intensity_scale, 0.f, 4.f);
        imgui.SliderInt("Shadow cascades", &fx.shadow_cascades, 1, 4);
        imgui.Separator();
        if (imgui.Button("Low", 90.f, 0.f)) {
          const auto q =
              engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
          fx.enable_ssao = q.enable_ssao;
          fx.enable_taa = q.enable_taa;
          fx.shadow_cascades = q.shadow_cascades;
        }
        if (imgui.Button("Med", 90.f, 0.f)) {
          const auto q =
              engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
          fx.enable_ssao = q.enable_ssao;
          fx.enable_taa = q.enable_taa;
          fx.shadow_cascades = q.shadow_cascades;
        }
        if (imgui.Button("High", 90.f, 0.f)) {
          const auto q =
              engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
          fx.enable_ssao = q.enable_ssao;
          fx.enable_taa = q.enable_taa;
          fx.shadow_cascades = q.shadow_cascades;
        }
        imgui.Separator();
        if (imgui.Button("Quit", 80.f, 0.f)) {
          app_ref.window().RequestClose();
        }
      }
      imgui.EndWindow();
    } else {
      if (imgui.BeginWindow("Hint", 16.f, 16.f, 240.f, 72.f)) {
        imgui.Text("F1 Effects | F2 Profiler");
      }
      imgui.EndWindow();
    }

    if (profiler_open) {
      if (imgui.BeginWindow("Profiler", 370.f, 48.f, 300.f, 280.f)) {
        char line[128];
        std::snprintf(line, sizeof(line), "dt=%.2f ms", app_ref.delta_time() * 1000.f);
        imgui.Text(line);
        imgui.Separator();
        imgui.Text("CPU");
        for (const auto& [name, ms] : profiler.samples_ms()) {
          std::snprintf(line, sizeof(line), "  %s: %.3f ms", name.c_str(), ms);
          imgui.Text(line);
        }
        imgui.Separator();
        imgui.Text("GPU (prev frame)");
        const auto gpu = app_ref.device().LastGpuPassTimings();
        if (gpu.empty()) {
          imgui.Text("  (n/a on this backend)");
        } else {
          for (const auto& t : gpu) {
            std::snprintf(line, sizeof(line), "  %s: %.3f ms", t.name.c_str(), t.ms);
            imgui.Text(line);
          }
        }
      }
      imgui.EndWindow();
    }

    app_ref.set_ui_want_capture(imgui.want_capture_mouse() || imgui.want_capture_keyboard());
    render.set_effect_tuning(fx);

    const float aspect = dh > 0.f ? dw / dh : 1.f;
    const auto scene = engine::render::RenderSceneExtractor::Extract(
        app_ref.world(), app_ref.camera(), aspect);
    profiler.Begin("DrawFrame");
    if (auto st = render.DrawFrame(app_ref.device(), scene, env, aspect, &sprites, nullptr); !st) {
      engine::LogError(st.message());
    }
    profiler.End("DrawFrame");
    profiler.Begin("ImGui");
    if (auto st = imgui.Render(app_ref.device()); !st) {
      engine::LogError(st.message());
    }
    profiler.End("ImGui");
    profiler.End("Frame");
  });
  return status ? 0 : 1;
}
