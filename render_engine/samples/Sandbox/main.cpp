#include "engine/app/application.h"

#include "engine/core/log.h"
#include "engine/media/media.h"
#include "engine/mixed/pick.h"
#include "engine/net/net_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/render/environment.h"
#include "engine/render/render_system.h"
#include "engine/render2d/sprite.h"
#include "engine/ui/retained_ui.h"

#include <filesystem>
#include <memory>
#include <string>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

int main() {
  engine::ApplicationDesc desc;
  desc.window.title = "Sandbox — lit cubes (WASD/QE + mouse, Esc quit)";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.05f, 0.07f, 0.1f, 1.f};

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  a.set_net(std::make_shared<engine::net::NetSystem>());
  a.camera().position = {0.f, 2.f, 6.f};

  // Scene: ground + a few cubes.
  auto ground = a.world().CreateNode("ground");
  {
    engine::scene::Transform t;
    t.position = {0, -0.5f, 0};
    t.scale = {8.f, 1.f, 8.f};
    a.world().set_local_transform(ground, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
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

  engine::render::Environment env;
  env.sun_direction = {0.35f, -1.f, 0.25f};
  env.sun_intensity = 2.8f;

  engine::render::RenderSystem render;
  engine::render::RenderSystemDesc rdesc;
  rdesc.lit_vs = std::filesystem::path(ENGINE_SHADER_DIR_A) / "lit_cube.vs.cso";
  rdesc.lit_ps = std::filesystem::path(ENGINE_SHADER_DIR_A) / "lit_cube.ps.cso";
  if (auto st = render.Init(a.device(), rdesc); !st) {
    engine::LogError(st.message());
    return 1;
  }

  auto physics = engine::physics::CreateBuiltinPhysicsWorld();
  engine::physics::RigidBodyDesc falling;
  falling.position = {0, 4, -2};
  const int phys_id = physics->CreateBox(falling);
  auto phys_node = a.world().CreateNode("phys_box");
  {
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(phys_node, mesh);
  }

  engine::ui::RetainedUi ui;
  ui.Label("hud", "WASD move | mouse look | Esc quit", 16, 16);
  ui.Button("quit", "Quit", 16, 48, 80, 28);

  std::vector<engine::render2d::Sprite> sprites;
  engine::render2d::Sprite s;
  s.position = {20, 100};
  s.size = {32, 32};
  s.sort_y = 100;
  sprites.push_back(s);

  auto audio = engine::media::CreateDefaultAudioDevice();
  engine::LogInfo(std::string("Audio backend: ") + audio->backend_name());

  const auto status = a.Run([&](engine::Application& app_ref) {
    physics->Step(app_ref.delta_time());
    {
      engine::scene::Transform t = app_ref.world().local_transform(phys_node);
      t.position = physics->body_position(phys_id);
      app_ref.world().set_local_transform(phys_node, t);
    }
    app_ref.world().UpdateTransforms();

    const float aspect = app_ref.window().height() > 0
                             ? static_cast<float>(app_ref.window().width()) /
                                   static_cast<float>(app_ref.window().height())
                             : 1.f;
    const auto scene = engine::render::RenderSceneExtractor::Extract(
        app_ref.world(), app_ref.camera(), aspect);
    if (auto st = render.DrawFrame(app_ref.device(), scene, env, aspect); !st) {
      engine::LogError(st.message());
    }

    // UI capture blocks fly look when hovering HUD button.
    const auto& snap = app_ref.window().input_snapshot();
    // mouse position approximate: not tracked absolutely — use WantCapture from hit if LMB.
    ui.set_want_capture(false);
    if (const auto hit = ui.HitTest(24, 60)) {
      if (*hit == "quit" && snap.mouse_left) {
        app_ref.window().RequestClose();
      }
    }
    (void)sprites;
  });
  return status ? 0 : 1;
}
