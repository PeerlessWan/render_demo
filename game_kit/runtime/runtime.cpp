#include "game_kit/runtime.h"

#include "game_kit/script.h"

#include "engine/app/application.h"

namespace game_kit {

GameRuntime::GameRuntime() : script_(std::make_unique<ScriptVm>()) {}

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

void GameRuntime::ClearPlayState() {
  timer_.Clear();
  coroutines_.Clear();
  scripts_.Clear();
  triggers_.Clear();
  entities_.Clear();
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
  levels_.Pump(app, *this);
  TickLogic(dt);
}

}  // namespace game_kit
