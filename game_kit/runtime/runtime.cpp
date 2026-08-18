#include "game_kit/runtime.h"

#include "game_kit/script.h"

#include "engine/app/application.h"
#include "engine/input/input_system.h"
#include "engine/platform/window.h"
#include "engine/ui/retained_ui.h"

#include <string>

namespace game_kit {

GameRuntime::GameRuntime() : script_(std::make_unique<ScriptVm>()) {
  events_.set_on_publish([this](std::string_view topic, std::string_view) {
    coroutines_.WakeTopic(topic);
  });
}

GameRuntime::~GameRuntime() = default;

std::filesystem::path GameRuntime::ResolveScriptPath(std::string_view path) const {
  std::filesystem::path p(path);
  if (p.is_absolute() || script_root_.empty()) {
    return p;
  }
  return script_root_ / p;
}

void GameRuntime::RegisterPrefab(std::string id, PrefabDocument doc) {
  prefabs_[std::move(id)] = std::move(doc);
}

const PrefabDocument* GameRuntime::FindPrefab(std::string_view id) const {
  auto it = prefabs_.find(std::string(id));
  if (it == prefabs_.end()) {
    return nullptr;
  }
  return &it->second;
}

engine::scene::NodeId GameRuntime::SpawnPrefab(std::string_view id,
                                               const engine::scene::Transform& world_trs) {
  const auto* doc = FindPrefab(id);
  if (!doc || !world_) {
    return engine::scene::kInvalidNode;
  }
  return Instantiate(*world_, *doc, world_trs, this);
}

void GameRuntime::RememberAsset(std::string id, engine::assets::AssetHandle handle) {
  assets_[std::move(id)] = std::move(handle);
}

const engine::assets::AssetHandle* GameRuntime::FindAsset(std::string_view id) const {
  auto it = assets_.find(std::string(id));
  if (it == assets_.end()) {
    return nullptr;
  }
  return &it->second;
}

void GameRuntime::QueueAssetReady(std::string id, bool ok) {
  asset_ready_.push_back({std::move(id), ok});
}

engine::input::InputSystem* GameRuntime::input() {
  if (input_) {
    return input_;
  }
  return app_ ? &app_->input() : nullptr;
}

void GameRuntime::ClearPlayState() {
  timer_.Clear();
  coroutines_.Clear();
  scripts_.Clear();
  triggers_.Clear();
  entities_.Clear();
  anims_.Clear();
  mixer_.StopAll();
  nav_.Clear();
  timeline_.Clear();
  asset_ready_.clear();
}

void GameRuntime::TickLogic(float dt) {
  logic_dt_ = paused_ ? 0.f : dt * time_scale_;
  if (world_) {
    scripts_.AttachHost(world_, this);
    if (script_) {
      script_->Attach(world_, this);
      script_->set_debug_hooks(script_debug_);
    }
  }
  if (logic_dt_ <= 0.f) {
    return;
  }
  if (world_) {
    triggers_.Tick(*world_, entities_, events_, scripts_);
  }
  if (physics_) {
    triggers_.TickPhysics(*physics_, entities_, events_, scripts_);
  }
  nav_.TickConfiguredSense(entities_, world_);
  nav_.TickFollow(entities_, world_, logic_dt_);
  anims_.Update(logic_dt_, events_, entities_, scripts_, world_);
  if (world_) {
    if (auto* e = entities_.FindByName(player_.entity_name)) {
      if (e->node != engine::scene::kInvalidNode && world_->valid(e->node)) {
        mixer_.set_listener(world_->local_transform(e->node).position);
      }
    }
  }
  mixer_.Tick();
  timeline_.Tick(logic_dt_, events_);
  for (auto& a : asset_ready_) {
    events_.Publish("asset.ready", a.first);
    if (script_) {
      (void)script_->CallNamed1("on_asset_ready", a.first);
    }
  }
  asset_ready_.clear();
  timer_.Tick(logic_dt_);
  if (script_) {
    (void)script_->CallUpdate(logic_dt_);
  }
  scripts_.Tick(logic_dt_);
  coroutines_.Tick(logic_dt_);
  coroutines_.ResumeLua();
}

void GameRuntime::Tick(engine::Application& app, float dt) {
  app_ = &app;
  world_ = &app.world();
  if (ui_) {
    const auto& snap = app.window().input_snapshot();
    const bool pressed = snap.mouse_left && !ui_mouse_was_down_;
    const auto evs = ui_->Pump(snap.mouse_x, snap.mouse_y, snap.mouse_left, pressed);
    for (const auto& e : evs) {
      if (e.type == engine::ui::UiEventType::Click) {
        events_.Publish(std::string("ui.click.") + e.id, e.id);
      }
    }
    ui_mouse_was_down_ = snap.mouse_left;
    app.set_ui_want_capture(ui_->want_capture());
  }
  levels_.Pump(app, *this, dt);
  TickLogic(dt);
}

}  // namespace game_kit
