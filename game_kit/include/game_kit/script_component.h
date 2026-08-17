#pragma once

#include "game_kit/script.h"

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

class GameRuntime;

using ScriptComponentId = std::uint32_t;
inline constexpr ScriptComponentId kInvalidScriptComponent = 0;

// Per-entity script binding. Hot reload resets the VM only — Device stays alive.
struct ScriptComponent {
  ScriptComponentId id = kInvalidScriptComponent;
  engine::scene::NodeId node = engine::scene::kInvalidNode;
  std::string path;
  ScriptVm vm;
  bool enabled = true;
};

class ScriptComponentWorld {
 public:
  ScriptComponentId Attach(engine::scene::NodeId node, std::string path);
  void Detach(ScriptComponentId id);
  [[nodiscard]] ScriptComponent* Get(ScriptComponentId id);
  [[nodiscard]] const std::vector<ScriptComponent>& all() const { return comps_; }

  engine::Status LoadFromDisk(ScriptComponent& c);
  // Safe hot reload: Reset VM + LoadFromDisk. Does not destroy Application/Device.
  engine::Status Reload(ScriptComponent& c);
  engine::Status ReloadPath(std::string_view path);

  void AttachHost(engine::scene::World* world, GameRuntime* rt);
  void Tick(float dt);

  void Clear();

 private:
  std::vector<ScriptComponent> comps_;
  ScriptComponentId next_id_ = 1;
  engine::scene::World* world_ = nullptr;
  GameRuntime* rt_ = nullptr;
};

}  // namespace game_kit
