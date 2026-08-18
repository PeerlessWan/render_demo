#pragma once

#include "engine/core/math.h"
#include "engine/scene/world.h"

#include "game_kit/entity.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

class EventBus;
class ScriptComponentWorld;

using TriggerId = std::uint32_t;
inline constexpr TriggerId kInvalidTrigger = 0;

// AABB volume in world space (center = node transform). Engine physics has no overlap
// callbacks — this is the game_kit trigger convention.
struct TriggerVolume {
  TriggerId id = kInvalidTrigger;
  std::string name;
  engine::scene::NodeId node = engine::scene::kInvalidNode;
  engine::Vec3 half_extents{0.5f, 0.5f, 0.5f};
  std::string target_entity;  // empty = any entity
  std::vector<EntityId> inside;
};

class TriggerWorld {
 public:
  TriggerId Add(std::string name, engine::scene::NodeId node, engine::Vec3 half_extents,
                std::string target_entity = {});
  void Remove(TriggerId id);
  [[nodiscard]] TriggerVolume* Get(TriggerId id);
  [[nodiscard]] const std::vector<TriggerVolume>& all() const { return vols_; }

  void Tick(engine::scene::World& world, EntityWorld& entities, EventBus& events,
            ScriptComponentWorld& scripts);

  void Clear();

 private:
  std::vector<TriggerVolume> vols_;
  TriggerId next_id_ = 1;
};

}  // namespace game_kit
