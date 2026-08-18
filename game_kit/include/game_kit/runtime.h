#pragma once

#include "game_kit/coroutine_scheduler.h"
#include "game_kit/entity.h"
#include "game_kit/event_bus.h"
#include "game_kit/level_flow.h"
#include "game_kit/player_controller.h"
#include "game_kit/prefab.h"
#include "game_kit/save.h"
#include "game_kit/script_component.h"
#include "game_kit/timer.h"
#include "game_kit/trigger.h"

#include "engine/assets/asset_handle.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {
class Application;
namespace physics {
class IPhysicsWorld;
}
namespace ui {
class RetainedUi;
}
namespace media {
class IAudioDevice;
}
}

namespace game_kit {

class ScriptVm;

class GameRuntime {
 public:
  GameRuntime();
  ~GameRuntime();

  GameRuntime(const GameRuntime&) = delete;
  GameRuntime& operator=(const GameRuntime&) = delete;

  LevelFlow& levels() { return levels_; }
  Timer& timer() { return timer_; }
  EventBus& events() { return events_; }
  EntityWorld& entities() { return entities_; }
  SaveSlots& saves() { return saves_; }
  ScriptComponentWorld& scripts() { return scripts_; }
  TriggerWorld& triggers() { return triggers_; }
  CoroutineScheduler& coroutines() { return coroutines_; }
  PlayerController& player() { return player_; }
  ScriptVm* script() { return script_.get(); }

  void set_paused(bool v) { paused_ = v; }
  [[nodiscard]] bool paused() const { return paused_; }
  void toggle_paused() { paused_ = !paused_; }

  void set_time_scale(float s) { time_scale_ = s < 0.f ? 0.f : s; }
  [[nodiscard]] float time_scale() const { return time_scale_; }

  void set_script_debug(bool v) { script_debug_ = v; }
  [[nodiscard]] bool script_debug() const { return script_debug_; }

  void set_script_root(std::filesystem::path root) { script_root_ = std::move(root); }
  [[nodiscard]] const std::filesystem::path& script_root() const { return script_root_; }
  [[nodiscard]] std::filesystem::path ResolveScriptPath(std::string_view path) const;

  void set_world(engine::scene::World* world) { world_ = world; }
  [[nodiscard]] engine::scene::World* world() { return world_; }
  [[nodiscard]] engine::Application* app() { return app_; }

  void set_physics(engine::physics::IPhysicsWorld* physics) { physics_ = physics; }
  [[nodiscard]] engine::physics::IPhysicsWorld* physics() { return physics_; }
  void set_ui(engine::ui::RetainedUi* ui) { ui_ = ui; }
  [[nodiscard]] engine::ui::RetainedUi* ui() { return ui_; }
  void set_audio(engine::media::IAudioDevice* audio) { audio_ = audio; }
  [[nodiscard]] engine::media::IAudioDevice* audio() { return audio_; }

  void RegisterPrefab(std::string id, PrefabDocument doc);
  [[nodiscard]] const PrefabDocument* FindPrefab(std::string_view id) const;
  engine::scene::NodeId SpawnPrefab(std::string_view id, const engine::scene::Transform& world_trs);

  void RememberAsset(std::string id, engine::assets::AssetHandle handle);
  [[nodiscard]] const engine::assets::AssetHandle* FindAsset(std::string_view id) const;

  void ClearPlayState();
  void Tick(engine::Application& app, float dt);
  void TickLogic(float dt);
  [[nodiscard]] float logic_dt() const { return logic_dt_; }

 private:
  LevelFlow levels_;
  Timer timer_;
  EventBus events_;
  EntityWorld entities_;
  SaveSlots saves_;
  ScriptComponentWorld scripts_;
  TriggerWorld triggers_;
  CoroutineScheduler coroutines_;
  PlayerController player_;
  std::unique_ptr<ScriptVm> script_;
  std::unordered_map<std::string, PrefabDocument> prefabs_;
  std::unordered_map<std::string, engine::assets::AssetHandle> assets_;
  std::filesystem::path script_root_;
  engine::Application* app_ = nullptr;
  engine::scene::World* world_ = nullptr;
  engine::physics::IPhysicsWorld* physics_ = nullptr;
  engine::ui::RetainedUi* ui_ = nullptr;
  engine::media::IAudioDevice* audio_ = nullptr;
  bool paused_ = false;
  bool script_debug_ = false;
  float time_scale_ = 1.f;
  float logic_dt_ = 0.f;
};

}  // namespace game_kit
