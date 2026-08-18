#pragma once

#include "engine/core/result.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {
class Application;
}

namespace game_kit {

class GameRuntime;

class LevelFlow {
 public:
  using LoadFn = std::function<void(engine::Application&, GameRuntime&)>;
  using UnloadFn = std::function<void(engine::Application&, GameRuntime&)>;

  void Register(std::string name, LoadFn load, UnloadFn unload = {});
  engine::Status Request(std::string_view name);
  void Pump(engine::Application& app, GameRuntime& rt);

  [[nodiscard]] const std::string& current() const { return current_; }
  [[nodiscard]] bool pending() const { return !pending_.empty(); }
  [[nodiscard]] bool transitioning() const { return transitioning_; }

 private:
  struct Level {
    std::string name;
    LoadFn load;
    UnloadFn unload;
  };
  std::vector<Level> levels_;
  std::string current_;
  std::string pending_;
  bool transitioning_ = false;
};

}  // namespace game_kit
