#include "game_kit/module.h"
#include "game_kit/prefab.h"
#include "game_kit/runtime.h"
#include "host_bootstrap.h"

#include "engine/core/log.h"

#include <memory>

namespace {

class PrefabPlaceModule final : public game_kit::IGameModule {
 public:
  PrefabPlaceModule() : IGameModule("prefab_place") {}

  engine::Status OnInit(engine::Application& app) override {
#ifdef GAME_KIT_SAMPLE_DIR
    rt_.set_script_root(GAME_KIT_SAMPLE_DIR);
#endif
    rt_.set_world(&app.world());
    game_kit::ClearWorld(app.world());
    engine::scene::Transform t;
    t.position = {0.f, 0.f, 0.f};
    (void)game_kit::Instantiate(app.world(), game_kit::MakeChestTagPrefab(), t, &rt_);
    t.position = {3.f, 0.f, 0.f};
    (void)game_kit::Instantiate(app.world(), game_kit::MakeTreePrefab(), t, &rt_);
    app.camera().position = {0.f, 2.5f, 8.f};
    app.camera().pitch = -0.2f;
    return engine::Status::Ok();
  }

  void OnUpdate(engine::Application& app, float dt) override { rt_.Tick(app, dt); }

 private:
  game_kit::GameRuntime rt_;
};

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "game_kit — prefab_place";
  KitParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  if (auto st = a.modules().Register(std::make_unique<PrefabPlaceModule>()); !st) {
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
