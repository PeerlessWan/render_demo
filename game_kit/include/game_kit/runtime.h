#pragma once

#include "game_kit/entity.h"
#include "game_kit/event_bus.h"
#include "game_kit/level_flow.h"
#include "game_kit/save.h"
#include "game_kit/timer.h"

#include <memory>
#include <string>

namespace engine {
class Application;
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
  ScriptVm* script() { return script_.get(); }

  void set_paused(bool v) { paused_ = v; }
  [[nodiscard]] bool paused() const { return paused_; }
  void toggle_paused() { paused_ = !paused_; }

  void Tick(engine::Application& app, float dt);
  [[nodiscard]] float logic_dt() const { return logic_dt_; }

 private:
  LevelFlow levels_;
  Timer timer_;
  EventBus events_;
  EntityWorld entities_;
  SaveSlots saves_;
  std::unique_ptr<ScriptVm> script_;
  bool paused_ = false;
  float logic_dt_ = 0.f;
};

}  // namespace game_kit
