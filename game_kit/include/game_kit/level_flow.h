#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {
class Application;
}

namespace game_kit {

class GameRuntime;

enum class LoadMode : std::uint8_t { Replace = 0, Additive = 1 };

class LevelFlow {
 public:
  using LoadFn = std::function<void(engine::Application&, GameRuntime&)>;
  using UnloadFn = std::function<void(engine::Application&, GameRuntime&)>;

  void Register(std::string name, LoadFn load, UnloadFn unload = {});
  engine::Status Request(std::string_view name, LoadMode mode = LoadMode::Replace,
                         float delay_seconds = 0.f);
  void TickLoading(float dt);
  void Pump(engine::Application& app, GameRuntime& rt, float dt);
  // Headless: advances delay and commits names/stack. Load/Unload run only if `app` is set.
  void Pump(GameRuntime& rt, float dt);
  engine::Status UnloadStacked(std::string_view name, engine::Application* app, GameRuntime& rt);
  engine::Status UnloadStacked(std::string_view name, GameRuntime& rt);

  [[nodiscard]] const std::string& current() const { return current_; }
  [[nodiscard]] const std::vector<std::string>& stacked() const { return stacked_; }
  [[nodiscard]] bool pending() const { return !pending_.empty() || delay_left_ > 0.f; }
  [[nodiscard]] bool transitioning() const { return transitioning_; }
  [[nodiscard]] bool loading() const { return delay_left_ > 0.f; }
  [[nodiscard]] float loading_progress() const;

 private:
  void Commit(engine::Application* app, GameRuntime& rt, const std::string& name, LoadMode mode);
  void PumpImpl(engine::Application* app, GameRuntime& rt, float dt);

  struct Level {
    std::string name;
    LoadFn load;
    UnloadFn unload;
  };
  std::vector<Level> levels_;
  std::string current_;
  std::vector<std::string> stacked_;
  std::string pending_;
  LoadMode pending_mode_ = LoadMode::Replace;
  float delay_total_ = 0.f;
  float delay_left_ = 0.f;
  bool transitioning_ = false;
};

}  // namespace game_kit
