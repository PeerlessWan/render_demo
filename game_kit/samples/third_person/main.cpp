#include "game_kit/module.h"
#include "game_kit/runtime.h"
#include "host_bootstrap.h"

#include "engine/core/log.h"
#include "engine/input/input_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/render/environment.h"
#include "engine/rhi/i_device.h"
#include "engine/scene/world.h"
#include "engine/ui/retained_ui.h"
#include "engine/ui/rml_ui.h"

#include <memory>
#include <vector>

namespace {

engine::scene::NodeId MakeBox(engine::scene::World& world, const char* name, engine::Vec3 pos,
                              engine::Vec3 scale) {
  const auto id = world.CreateNode(name);
  engine::scene::Transform t;
  t.position = pos;
  t.scale = scale;
  world.set_local_transform(id, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  mesh.local_bounds = {scale * -0.5f, scale * 0.5f};
  world.set_mesh(id, mesh);
  return id;
}

class CorridorModule final : public game_kit::IGameModule {
 public:
  CorridorModule() : IGameModule("third_person") {}

  engine::Status OnInit(engine::Application& app) override {
    app.set_move_speed(0.f);
    app.set_look_with_lmb(false);
    app.set_look_with_rmb(false);
    game_kit::PlayerController::InstallPlayDefaults(app.input());

#ifdef GAME_KIT_SAMPLE_DIR
    rt_.set_script_root(GAME_KIT_SAMPLE_DIR);
#endif

    phys_ = engine::physics::CreateDefaultPhysicsWorld();
    if (!phys_) {
      return engine::Status::Fail("physics world");
    }
    rt_.set_physics(phys_.get());

    hud_ = engine::ui::CreateRetainedUiBackend();
    (void)engine::ui::LoadRmlDocumentFromMemory(*hud_,
                                                "<rml><head></head><body>GK3</body></rml>");
    hud_->Panel("hud", 16.f, 16.f, 280.f, 96.f);
    hud_->Label("msg", "Reach the far cube", 28.f, 36.f);
    hud_->Button("save", "Save", 28.f, 64.f, 72.f, 22.f);
    rt_.set_ui(hud_.get());
    rt_.set_script_debug(true);

    engine::physics::RigidBodyDesc floor;
    floor.position = {0.f, -0.5f, 8.f};
    floor.half_extents = {4.f, 0.5f, 12.f};
    floor.mass = 0.f;
    (void)phys_->CreateBox(floor);

    engine::physics::RigidBodyDesc player;
    player.position = {0.f, 1.f, 0.f};
    player.half_extents = {0.4f, 0.9f, 0.4f};
    player.mass = 1.f;
    player_body_ = phys_->CreateBox(player);
    rt_.player().physics_body = player_body_;
    rt_.player().entity_name = "player";

    goal_pos_ = {0.f, 1.f, 16.f};

    rt_.events().Subscribe("ui.click.save", [this](std::string_view) {
      (void)game_kit::SaveSnapshot(rt_.saves(), 0, game_kit::CaptureSnapshot(rt_, rt_.world()));
      if (hud_) {
        hud_->set_text("msg", "Saved");
      }
    });
    rt_.events().Subscribe("level.complete", [this](std::string_view) {
      done_ = true;
      if (hud_) {
        hud_->set_text("msg", "Done");
      }
      engine::LogInfo("GK3 corridor complete");
    });

    rt_.levels().Register(
        "corridor",
        [this](engine::Application& a, game_kit::GameRuntime& rt) {
          MakeBox(a.world(), "floor", {0.f, -0.05f, 8.f}, {8.f, 0.1f, 24.f});
          const auto p = MakeBox(a.world(), "player", {0.f, 1.f, 0.f}, {0.8f, 1.8f, 0.8f});
          const auto goal = MakeBox(a.world(), "goal", goal_pos_, {1.f, 1.f, 1.f});
          rt.entities().Create("player", p);
          rt.entities().Create("goal", goal);
          rt.triggers().Add("goal", goal, {1.6f, 1.6f, 1.6f}, "player");
          rt.nav().AddObstacle({2.f, 0.5f, 8.f}, {0.6f, 0.5f, 0.6f});
          (void)rt.nav().BakeFromObstacles();
          const auto sid =
              rt.scripts().Attach(goal, rt.ResolveScriptPath("scripts/goal.lua").string());
          if (auto* sc = rt.scripts().Get(sid)) {
            (void)rt.scripts().LoadFromDisk(*sc);
          }
        },
        [](engine::Application& a, game_kit::GameRuntime&) { game_kit::ClearWorld(a.world()); });
    (void)rt_.levels().Request("corridor");
    return engine::Status::Ok();
  }

  void OnUpdate(engine::Application& app, float dt) override {
    rt_.Tick(app, dt);
    if (rt_.paused() || done_ || !phys_) {
      return;
    }

    rt_.player().TickMove(app, rt_, dt);
    phys_->Step(dt);
    rt_.player().TickView(app, rt_);
  }

  std::vector<engine::rhi::ScreenQuad> HudQuads() const {
    std::vector<engine::rhi::ScreenQuad> quads;
    if (!hud_) {
      return quads;
    }
    for (const auto& r : hud_->BuildDrawList()) {
      engine::rhi::ScreenQuad q;
      q.x0 = r.x0;
      q.y0 = r.y0;
      q.x1 = r.x1;
      q.y1 = r.y1;
      q.color = r.color;
      quads.push_back(q);
    }
    return quads;
  }

 private:
  game_kit::GameRuntime rt_;
  std::unique_ptr<engine::physics::IPhysicsWorld> phys_;
  std::unique_ptr<engine::ui::RetainedUi> hud_;
  int player_body_ = -1;
  engine::Vec3 goal_pos_{};
  bool done_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "game_kit — third_person";
  KitParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  auto module = std::make_unique<CorridorModule>();
  auto* corridor = module.get();
  if (auto st = a.modules().Register(std::move(module)); !st) {
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
    const auto quads = corridor->HudQuads();
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect, nullptr,
                                   &quads);
        !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
