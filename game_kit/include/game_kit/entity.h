#pragma once

#include "engine/scene/world.h"

#include "game_kit/ai_state.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

using EntityId = std::uint32_t;
inline constexpr EntityId kInvalidEntity = 0;

struct Entity {
  EntityId id = kInvalidEntity;
  engine::scene::NodeId node = engine::scene::kInvalidNode;
  std::string name;
  std::string script_path;
  bool script_frozen = false;
  AiStateMachine ai;
  bool active = true;
  std::vector<std::string> tags;
  int physics_body = -1;
  bool physics_is_trigger = false;

  void AddTag(std::string tag);
  [[nodiscard]] bool HasTag(std::string_view tag) const;
};

class EntityWorld {
 public:
  EntityId Create(std::string name, engine::scene::NodeId node = engine::scene::kInvalidNode);
  void Destroy(EntityId id);
  [[nodiscard]] Entity* Get(EntityId id);
  [[nodiscard]] const Entity* Get(EntityId id) const;
  [[nodiscard]] Entity* FindByName(std::string_view name);
  [[nodiscard]] const Entity* FindByName(std::string_view name) const;
  [[nodiscard]] Entity* FindByNode(engine::scene::NodeId node);
  [[nodiscard]] const Entity* FindByNode(engine::scene::NodeId node) const;
  [[nodiscard]] std::vector<Entity*> FindByTag(std::string_view tag);
  void Deactivate(EntityId id);
  EntityId Acquire(std::string name, engine::scene::NodeId node = engine::scene::kInvalidNode);
  void Release(EntityId id);
  [[nodiscard]] const std::vector<Entity>& all() const { return entities_; }
  void Clear();

 private:
  std::vector<Entity> entities_;
  EntityId next_id_ = 1;
};

}  // namespace game_kit
