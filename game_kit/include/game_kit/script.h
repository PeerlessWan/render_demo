#pragma once

#include "engine/core/result.h"

#include <memory>
#include <string>
#include <string_view>

namespace engine::scene {
class World;
}

namespace game_kit {

class GameRuntime;

// Lua 5.4 VM wrapper. Bindings are a whitelist; errors freeze the script, never the Device.
class ScriptVm {
 public:
  ScriptVm();
  ~ScriptVm();

  ScriptVm(const ScriptVm&) = delete;
  ScriptVm& operator=(const ScriptVm&) = delete;
  ScriptVm(ScriptVm&&) noexcept;
  ScriptVm& operator=(ScriptVm&&) noexcept;

  void Attach(engine::scene::World* world, GameRuntime* rt);
  engine::Status LoadString(std::string_view source, std::string_view chunk_name);
  engine::Status LoadFile(std::string_view path);
  engine::Status CallUpdate(float dt);
  void Reset();

  [[nodiscard]] bool available() const;
  [[nodiscard]] bool frozen() const { return frozen_; }
  [[nodiscard]] const std::string& last_error() const { return last_error_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool frozen_ = false;
  std::string last_error_;
};

}  // namespace game_kit
