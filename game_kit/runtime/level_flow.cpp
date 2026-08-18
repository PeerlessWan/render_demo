#include "game_kit/level_flow.h"

#include "game_kit/runtime.h"

#include "engine/app/application.h"
#include "engine/core/log.h"

#include <algorithm>

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

engine::Status LevelFlow::Request(std::string_view name, LoadMode mode, float delay_seconds) {
  for (const auto& l : levels_) {
    if (l.name == name) {
      pending_ = l.name;
      pending_mode_ = mode;
      delay_total_ = delay_seconds < 0.f ? 0.f : delay_seconds;
      delay_left_ = delay_total_;
      return engine::Status::Ok();
    }
  }
  return engine::Status::Fail("unknown level: " + std::string(name));
}

float LevelFlow::loading_progress() const {
  if (delay_total_ <= 0.f) {
    return pending_.empty() ? 1.f : 0.f;
  }
  return std::clamp(1.f - delay_left_ / delay_total_, 0.f, 1.f);
}

void LevelFlow::TickLoading(float dt) {
  if (pending_.empty() || delay_left_ <= 0.f) {
    return;
  }
  delay_left_ -= dt > 0.f ? dt : 0.f;
  if (delay_left_ < 0.f) {
    delay_left_ = 0.f;
  }
}

void LevelFlow::Commit(engine::Application* app, GameRuntime& rt, const std::string& name,
                       LoadMode mode) {
  if (mode == LoadMode::Replace) {
    if (!current_.empty()) {
      for (const auto& l : levels_) {
        if (l.name == current_ && l.unload) {
          if (app) {
            l.unload(*app, rt);
          }
          break;
        }
      }
      rt.ClearPlayState();
    }
    stacked_.clear();
  }

  for (const auto& l : levels_) {
    if (l.name == name) {
      current_ = name;
      stacked_.push_back(name);
      if (l.load && app) {
        l.load(*app, rt);
      }
      engine::LogInfo("LevelFlow loaded: " + current_);
      return;
    }
  }
}

void LevelFlow::Pump(engine::Application& app, GameRuntime& rt, float dt) {
  PumpImpl(&app, rt, dt);
}

void LevelFlow::Pump(GameRuntime& rt, float dt) { PumpImpl(nullptr, rt, dt); }

void LevelFlow::PumpImpl(engine::Application* app, GameRuntime& rt, float dt) {
  transitioning_ = false;
  if (pending_.empty()) {
    return;
  }
  transitioning_ = true;
  TickLoading(dt);
  if (delay_left_ > 0.f) {
    return;
  }
  const std::string name = pending_;
  const LoadMode mode = pending_mode_;
  pending_.clear();
  Commit(app, rt, name, mode);
  transitioning_ = false;
}

engine::Status LevelFlow::UnloadStacked(std::string_view name, GameRuntime& rt) {
  return UnloadStacked(name, nullptr, rt);
}

engine::Status LevelFlow::UnloadStacked(std::string_view name, engine::Application* app,
                                        GameRuntime& rt) {
  const std::string n(name);
  bool found = false;
  for (auto it = stacked_.begin(); it != stacked_.end();) {
    if (*it == n) {
      found = true;
      it = stacked_.erase(it);
    } else {
      ++it;
    }
  }
  if (!found) {
    return engine::Status::Fail("level not stacked: " + n);
  }
  for (const auto& l : levels_) {
    if (l.name == n && l.unload && app) {
      l.unload(*app, rt);
      break;
    }
  }
  if (current_ == n) {
    current_ = stacked_.empty() ? std::string{} : stacked_.back();
  }
  engine::LogInfo("LevelFlow unloaded: " + n);
  return engine::Status::Ok();
}

}  // namespace game_kit
