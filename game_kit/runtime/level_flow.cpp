#include "game_kit/level_flow.h"

#include "game_kit/runtime.h"

#include "engine/core/log.h"

namespace game_kit {

void LevelFlow::Register(std::string name, LoadFn load) {
  for (auto& l : levels_) {
    if (l.name == name) {
      l.load = std::move(load);
      return;
    }
  }
  levels_.push_back(Level{std::move(name), std::move(load)});
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
  if (pending_.empty()) {
    return;
  }
  const std::string name = pending_;
  pending_.clear();
  for (const auto& l : levels_) {
    if (l.name == name) {
      current_ = name;
      if (l.load) {
        l.load(app, rt);
      }
      engine::LogInfo("LevelFlow loaded: " + current_);
      return;
    }
  }
}

}  // namespace game_kit
