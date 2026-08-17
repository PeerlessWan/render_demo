#pragma once

#include "engine/scene/world.h"

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
};

class EntityWorld {
 public:
  EntityId Create(std::string name, engine::scene::NodeId node = engine::scene::kInvalidNode);
  void Destroy(EntityId id);
  [[nodiscard]] Entity* Get(EntityId id);
  [[nodiscard]] const Entity* Get(EntityId id) const;
  [[nodiscard]] Entity* FindByName(std::string_view name);
  [[nodiscard]] const std::vector<Entity>& all() const { return entities_; }
  void Clear();

 private:
  std::vector<Entity> entities_;
  EntityId next_id_ = 1;
};

}  // namespace game_kit
