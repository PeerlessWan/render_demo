#include "game_kit/module.h"
#include "game_kit/runtime.h"
#include "io/world_save.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/sfx.h"
#include "ui/hud.h"
#include "world/batch.h"
#include "world/trace.h"

#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/input/input_system.h"
#include "engine/platform/window.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_scene.h"
#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"
#include "engine/ui/retained_ui.h"
#include "engine/ui/rml_ui.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

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
  r.sky_vs = dir / "skybox.vs.cso";
  r.sky_ps = dir / "skybox.ps.cso";
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  r.quality.enable_ssao = false;
  r.quality.enable_taa = false;
  return r;
}

class SurvivalModule final : public game_kit::IGameModule {
 public:
  SurvivalModule() : IGameModule("minecraft_survival") {}

  engine::Status OnInit(engine::Application& app) override {
    app.set_move_speed(0.f);
    app.set_look_with_lmb(false);
    app.set_look_with_rmb(true);
    app.set_hide_cursor_on_look(true);
    hud_ = engine::ui::CreateRetainedUiBackend();
    mc::InitSfx(&sfx_);
    if (app.window().is_headless()) {
      EnterWorld(app, false, false);
    }
    return engine::Status::Ok();
  }

  void OnShutdown(engine::Application&) override {
    if (in_world_) {
      (void)mc::SaveWorld(st_, save_dir_);
    }
  }

  void OnUpdate(engine::Application& app, float dt) override {
    const auto& snap = app.window().input_snapshot();
    const bool lmb = snap.mouse_left;
    const bool rmb = snap.mouse_right;
    const bool mmb = snap.mouse_middle;
    const bool lmb_pressed = lmb && !lmb_held_;
    const bool rmb_pressed = rmb && !rmb_held_;
    const bool mmb_pressed = mmb && !mmb_held_;
    lmb_held_ = lmb;
    rmb_held_ = rmb;
    mmb_held_ = mmb;

    if (!in_world_) {
      if (hud_) {
        std::vector<engine::rhi::ScreenQuad> dummy;
        std::vector<mc::SlotHit> hits;
        mc::HudParams hp;
        hp.in_menu = true;
        mc::BuildHud(*hud_, st_.player, nullptr, static_cast<int>(app.window().width()),
                     static_cast<int>(app.window().height()), hp, &dummy, &hits);
        const auto ev = hud_->Pump(snap.mouse_x, snap.mouse_y, lmb, lmb_pressed);
        auto enter = [&](bool load, bool creative) { EnterWorld(app, load, creative); };
        for (const auto& e : ev) {
          if (e.type != engine::ui::UiEventType::Click) {
            continue;
          }
          if (e.id == "menu_new") {
            enter(false, false);
          } else if (e.id == "menu_load") {
            enter(true, false);
          } else if (e.id == "menu_creative") {
            enter(false, true);
          } else if (e.id == "menu_quit") {
            app.window().RequestClose();
          }
        }
        const bool k1 = snap.keys[0x31] != 0;
        const bool k2 = snap.keys[0x32] != 0;
        const bool k3 = snap.keys[0x33] != 0;
        const bool k4 = snap.keys[0x34] != 0;
        if (k1 && !menu_key_held_) {
          enter(false, false);
        } else if (k2 && !menu_key_held_) {
          enter(true, false);
        } else if (k3 && !menu_key_held_) {
          enter(false, true);
        } else if (k4 && !menu_key_held_) {
          app.window().RequestClose();
        }
        menu_key_held_ = k1 || k2 || k3 || k4;
      }
      return;
    }

    const bool p_down = snap.keys[0x50] != 0;
    const bool i_down = snap.keys[0x49] != 0;
    const bool f_down = snap.keys[0x46] != 0;
    const bool f3_down = snap.keys[0x72] != 0;
    const bool r_down = snap.keys[0x52] != 0;
    const bool f5_down = snap.keys[0x74] != 0;
    const bool br_down = snap.keys[0xDB] != 0;  // [
    const bool bk_down = snap.keys[0xDD] != 0;  // ]

    mc::GameInput in;
    in.snap = &snap;
    in.lmb = lmb && !st_.player.ui_open;
    in.rmb = rmb;
    in.mmb = mmb;
    in.lmb_pressed = lmb_pressed;
    in.rmb_pressed = rmb_pressed;
    in.mmb_pressed = mmb_pressed;
    in.mouse_x = snap.mouse_x;
    in.mouse_y = snap.mouse_y;
    in.toggle_pause = p_down && !p_held_;
    in.toggle_inv = i_down && !i_held_;
    in.toggle_f3 = f3_down && !f3_held_;
    in.toggle_creative = f_down && !f_held_;
    in.eat_or_respawn = r_down && !r_held_;
    in.save = f5_down && !f5_held_;
    in.view_in = bk_down && !view_in_held_;
    in.view_out = br_down && !view_out_held_;
    p_held_ = p_down;
    i_held_ = i_down;
    f_held_ = f_down;
    f3_held_ = f3_down;
    r_held_ = r_down;
    f5_held_ = f5_down;
    view_in_held_ = bk_down;
    view_out_held_ = br_down;

    if (!st_.player.ui_open) {
      if (app.input().key_down(engine::input::Key::W)) {
        in.wish.z += 1.f;
      }
      if (app.input().key_down(engine::input::Key::S)) {
        in.wish.z -= 1.f;
      }
      if (app.input().key_down(engine::input::Key::A)) {
        in.wish.x -= 1.f;
      }
      if (app.input().key_down(engine::input::Key::D)) {
        in.wish.x += 1.f;
      }
      if (st_.player.flying) {
        if (app.input().key_down(engine::input::Key::Space)) {
          in.wish.y += 1.f;
        }
        if (snap.keys[VK_SHIFT]) {
          in.wish.y -= 1.f;
        }
      }
      in.jump = app.input().key_down(engine::input::Key::Space);
      in.sneak = snap.keys[VK_SHIFT] != 0;
    }

    rt_.Tick(app, paused_ ? 0.f : dt);
    {
      std::vector<engine::rhi::ScreenQuad> ignore;
      FillHud(app, static_cast<int>(app.window().width()), static_cast<int>(app.window().height()),
              &ignore);
    }
    const float hp_before = st_.player.hp;
    const bool breaking_before = st_.player.breaking;
    const bool need_save = mc::TickGameplay(&st_, app.camera(), in, dt, &paused_, &f3_, hud_hits_);
    mc::SyncCamera(st_.player, &app.camera());
    if (st_.player.hp < hp_before - 0.01f) {
      mc::PlayHurt(&sfx_);
    }
    if (st_.player.breaking && !breaking_before) {
      mc::PlayClick(&sfx_);
    }
    if (need_save) {
      (void)mc::SaveWorld(st_, save_dir_);
      engine::LogInfo("world saved");
    }
    app.set_ui_want_capture(st_.player.ui_open || paused_);
  }

  void DrawWorld(engine::Application& app, engine::render::RenderSystem& render,
                 engine::render::Environment& env, float aspect,
                 const std::vector<engine::rhi::ScreenQuad>& quads) {
    if (in_world_) {
      mc::SyncCamera(st_.player, &app.camera());
    }
    st_.clock.Apply(&env);
    app.set_clear_color(env.clear_color);
    std::vector<engine::render::LocalLight> lights;
    if (in_world_) {
      mc::CollectTorchLights(st_.world, app.camera().position, 2, &lights);
    }
    render.set_local_lights(lights);
    render.set_shadows_enabled(false);
    if (in_world_) {
      std::vector<engine::rhi::LitDrawItem> opaque;
      std::vector<engine::rhi::LitDrawItem> water;
      mc::CollectVisible(st_.world, app.camera().position, st_.view_radius, &opaque, &water);
      std::vector<engine::rhi::LitDrawItem> drops;
      mc::CollectDrops(st_, &drops);
      opaque.insert(opaque.end(), drops.begin(), drops.end());
      for (const auto& m : st_.mobs) {
        if (!m.alive) {
          continue;
        }
        engine::rhi::LitDrawItem item;
        item.world = engine::Mat4::TRS(m.pos + engine::Vec3{0.f, 0.5f, 0.f}, engine::Quat::Identity(),
                                       engine::Vec3{0.7f, 1.f, 0.7f});
        item.use_albedo = false;
        item.mesh_slot = 0;
        if (m.kind == mc::MobKind::Cow) {
          item.color = {0.62f, 0.42f, 0.28f, 1.f};
        } else if (m.kind == mc::MobKind::Pig) {
          item.color = {0.86f, 0.48f, 0.52f, 1.f};
        } else if (m.kind == mc::MobKind::Sheep) {
          item.color = {0.88f, 0.88f, 0.86f, 1.f};
        } else {
          item.color = {0.22f, 0.38f, 0.22f, 1.f};
        }
        opaque.push_back(item);
      }
      mc::LitSubmit sub;
      mc::PrepareLitSubmit(opaque, app.camera().position, &sub);
      mc::QueueWorldDraws(app.device(), render, sub, water);
    }
    engine::render::RenderScene scene = app.render_scene();
    scene.camera = app.camera();
    if (auto st = render.DrawFrame(app.device(), scene, env, aspect, nullptr, &quads); !st) {
      engine::LogError(st.message());
    }
  }

  void FillHud(engine::Application& app, int w, int h, std::vector<engine::rhi::ScreenQuad>* quads) {
    if (!hud_) {
      return;
    }
    mc::HudParams hp;
    hp.paused = paused_;
    hp.in_menu = !in_world_;
    hp.f3 = f3_;
    hp.yaw = app.camera().yaw;
    hp.pitch = app.camera().pitch;
    hp.view_radius = st_.view_radius;
    hp.mouse_x = app.window().input_snapshot().mouse_x;
    hp.mouse_y = app.window().input_snapshot().mouse_y;
    if (in_world_) {
      hp.look = mc::TraceBlocks(st_.world, mc::Eye(st_.player), mc::LookDir(app.camera()), 6.f);
      if (hp.look.hit) {
        hp.look_id = st_.world.Get(hp.look.x, hp.look.y, hp.look.z);
        hp.break_need = mc::BreakTime(hp.look_id, st_.player.inv.Hotbar().id);
      }
    }
    mc::BuildHud(*hud_, st_.player, &st_.boxes, w, h, hp, quads, &hud_hits_);
  }

 private:
  void EnterWorld(engine::Application& app, bool load, bool creative) {
    save_dir_ = std::filesystem::path("worlds") / "slot0";
    st_ = mc::GameState{};
    const bool loaded = load && std::filesystem::exists(save_dir_ / "level.json");
    if (loaded) {
      if (auto st = mc::LoadWorld(&st_, save_dir_); !st) {
        engine::LogWarn(st.message());
        st_.world.set_seed(1);
        mc::SpawnOnSurface(st_.world, &st_.player);
        mc::PlantStarterGrove(st_.world, st_.player.pos);
        mc::GiveSurvivalKit(&st_.player);
      }
    } else {
      st_.world.set_seed(1);
      mc::SpawnOnSurface(st_.world, &st_.player);
      mc::PlantStarterGrove(st_.world, st_.player.pos);
      if (creative) {
        st_.player.creative = true;
        st_.player.flying = true;
        mc::FillCreativeInventory(&st_.player.inv);
      } else {
        mc::GiveSurvivalKit(&st_.player);
      }
    }
    st_.world.StreamAround(static_cast<int>(st_.player.pos.x), static_cast<int>(st_.player.pos.z), 5);
    in_world_ = true;
    app.camera().yaw = 0.f;
    app.camera().pitch = -0.55f;
    mc::SyncCamera(st_.player, &app.camera());
    (void)rt_.saves().Write(0, save_dir_.string());
  }

  game_kit::GameRuntime rt_;
  mc::GameState st_;
  mc::Sfx sfx_;
  std::unique_ptr<engine::ui::RetainedUi> hud_;
  std::vector<mc::SlotHit> hud_hits_;
  std::filesystem::path save_dir_{"worlds/slot0"};
  bool in_world_ = false;
  bool paused_ = false;
  bool f3_ = false;
  bool p_held_ = false;
  bool i_held_ = false;
  bool f_held_ = false;
  bool f3_held_ = false;
  bool r_held_ = false;
  bool f5_held_ = false;
  bool lmb_held_ = false;
  bool rmb_held_ = false;
  bool mmb_held_ = false;
  bool view_in_held_ = false;
  bool view_out_held_ = false;
  bool menu_key_held_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "minecraft survival";
  ParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  auto module = std::make_unique<SurvivalModule>();
  auto* survival = module.get();
  if (auto st = a.modules().Register(std::move(module)); !st) {
    engine::LogError(st.message());
    return 1;
  }

  engine::render::Environment env;
  env.skybox_enabled = false;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }
  {
    auto fx = render.effect_tuning();
    fx.enable_shadows = false;
    fx.enable_ssao = false;
    fx.enable_taa = false;
    fx.enable_bloom = false;
    fx.enable_reflection_probe = false;
    fx.enable_ibl = false;
    fx.enable_skybox = false;
    fx.ambient_scale = 1.2f;
    fx.sun_intensity = 3.2f;
    fx.exposure = 1.05f;
    render.set_effect_tuning(fx);
  }

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float h = static_cast<float>(app_ref.window().height());
    const float w = static_cast<float>(app_ref.window().width());
    const float aspect = h > 0.f ? w / h : 1.f;
    std::vector<engine::rhi::ScreenQuad> quads;
    survival->FillHud(app_ref, static_cast<int>(w), static_cast<int>(h), &quads);
    survival->DrawWorld(app_ref, render, env, aspect, quads);
  });
  return status ? 0 : 1;
}
