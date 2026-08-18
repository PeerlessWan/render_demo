#pragma once

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include "game_kit/entity.h"

#include <memory>
#include <string>
#include <string_view>

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
  void BindSelf(engine::scene::NodeId node, EntityId entity = kInvalidEntity);
  void set_debug_hooks(bool enabled);

  engine::Status LoadString(std::string_view source, std::string_view chunk_name);
  engine::Status LoadFile(std::string_view path);
  engine::Status CallNamed(std::string_view name);
  engine::Status CallUpdate(float dt);
  engine::Status CallTrigger(bool enter, std::string_view other);
  void Reset();
  void Freeze(std::string_view message);

  [[nodiscard]] bool available() const;
  [[nodiscard]] bool frozen() const { return frozen_; }
  [[nodiscard]] const std::string& last_error() const { return last_error_; }
  [[nodiscard]] void* lua_state() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool frozen_ = false;
  std::string last_error_;
};

}  // namespace game_kit
