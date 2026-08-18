#include "game_kit/level_flow.h"

#include "game_kit/runtime.h"

#include "engine/core/log.h"

namespace game_kit {

void LevelFlow::Register(std::string name, LoadFn load, UnloadFn unload) {
  for (auto& l : levels_) {
    if (l.name == name) {
      l.load = std::move(load);
      l.unload = std::move(unload);
      return;
    }
  }
  levels_.push_back(Level{std::move(name), std::move(load), std::move(unload)});
}

engine::Status LevelFlow::Request(std::string_view name) {
  for (const auto& l : levels_) {
    if (l.name == name) {
      pending_ = l.name;
      return engine::Status::Ok();
    }
  }
  return engine::Status::Fail("unknown level: " + std::string(name));
}

void LevelFlow::Pump(engine::Application& app, GameRuntime& rt) {
  transitioning_ = false;
  if (pending_.empty()) {
    return;
  }
  transitioning_ = true;
  const std::string name = pending_;
  pending_.clear();

  if (!current_.empty()) {
    for (const auto& l : levels_) {
      if (l.name == current_ && l.unload) {
        l.unload(app, rt);
        break;
      }
    }
    rt.ClearPlayState();
  }

  for (const auto& l : levels_) {
    if (l.name == name) {
      current_ = name;
      if (l.load) {
        l.load(app, rt);
      }
      engine::LogInfo("LevelFlow loaded: " + current_);
      transitioning_ = false;
      return;
    }
  }
  transitioning_ = false;
}

}  // namespace game_kit
