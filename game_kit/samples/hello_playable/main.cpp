#include "game_kit/module.h"
#include "game_kit/runtime.h"
#include "game_kit/script.h"
#include "host_bootstrap.h"

#include "engine/core/log.h"
#include "engine/input/input_system.h"
#include "engine/render/environment.h"
#include "engine/scene/world.h"

#include <memory>

namespace {

void ClearScene(engine::scene::World& world) { game_kit::ClearWorld(world); }

class PlayableModule final : public game_kit::IGameModule {
 public:
  PlayableModule() : IGameModule("hello_playable") {}

  engine::Status OnInit(engine::Application& app) override {
#ifdef GAME_KIT_SAMPLE_DIR
    rt_.set_script_root(GAME_KIT_SAMPLE_DIR);
#endif
    auto load_marker = [](const char* node_name, engine::Vec3 pos) {
      return [node_name, pos](engine::Application& a, game_kit::GameRuntime& rt) {
        ClearScene(a.world());
        const auto n = a.world().CreateNode(node_name);
        engine::scene::Transform t;
        t.position = pos;
        a.world().set_local_transform(n, t);
        engine::scene::MeshRenderer mesh;
        mesh.mesh_id = "cube";
        a.world().set_mesh(n, mesh);
        rt.entities().Create("marker", n);
      };
    };
    rt_.levels().Register("start", load_marker("marker", {0.f, 0.5f, 0.f}),
                          [](engine::Application& a, game_kit::GameRuntime&) { ClearScene(a.world()); });
    rt_.levels().Register("next", load_marker("marker2", {2.f, 0.5f, 0.f}),
                          [](engine::Application& a, game_kit::GameRuntime&) { ClearScene(a.world()); });
    (void)rt_.levels().Request("start");
    (void)rt_.saves().Write(0, "hello_playable");
    if (auto* vm = rt_.script()) {
      (void)vm->LoadString(R"(
        log("hello_playable script")
        delay(0.0, function() log("hello delay fired") end)
        function on_update(dt) end
      )",
                           "hello");
    }
    (void)app;
    return engine::Status::Ok();
  }

  void OnUpdate(engine::Application& app, float dt) override {
    if (app.input().key_down(engine::input::Key::Q) && !q_held_) {
      rt_.toggle_paused();
      engine::LogInfo(rt_.paused() ? "paused" : "resumed");
    }
    q_held_ = app.input().key_down(engine::input::Key::Q);
    if (app.input().key_down(engine::input::Key::E) && !e_held_) {
      const auto next = rt_.levels().current() == "start" ? "next" : "start";
      (void)rt_.levels().Request(next);
    }
    e_held_ = app.input().key_down(engine::input::Key::E);
    if (app.input().key_down(engine::input::Key::Space) && !space_held_) {
      const float next = rt_.time_scale() > 0.9f ? 0.25f : (rt_.time_scale() > 0.01f ? 0.f : 1.f);
      rt_.set_time_scale(next);
      engine::LogInfo("time_scale " + std::to_string(rt_.time_scale()));
    }
    space_held_ = app.input().key_down(engine::input::Key::Space);
    rt_.Tick(app, dt);
  }

 private:
  game_kit::GameRuntime rt_;
  bool q_held_ = false;
  bool e_held_ = false;
  bool space_held_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "game_kit — hello_playable";
  KitParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  if (auto st = a.modules().Register(std::make_unique<PlayableModule>()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), KitLitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float h = static_cast<float>(app_ref.window().height());
    const float aspect = h > 0.f ? static_cast<float>(app_ref.window().width()) / h : 1.f;
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect); !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
