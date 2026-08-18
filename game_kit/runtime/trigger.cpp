#include "game_kit/trigger.h"

#include "game_kit/event_bus.h"
#include "game_kit/script_component.h"

#include <cmath>
#include <sstream>

namespace game_kit {
namespace {

bool AabbContains(const engine::Vec3& center, const engine::Vec3& half, const engine::Vec3& p) {
  return std::abs(p.x - center.x) <= half.x && std::abs(p.y - center.y) <= half.y &&
         std::abs(p.z - center.z) <= half.z;
}

}  // namespace

TriggerId TriggerWorld::Add(std::string name, engine::scene::NodeId node, engine::Vec3 half_extents,
                            std::string target_entity) {
  TriggerVolume v;
  v.id = next_id_++;
  v.name = std::move(name);
  v.node = node;
  v.half_extents = half_extents;
  v.target_entity = std::move(target_entity);
  vols_.push_back(std::move(v));
  return vols_.back().id;
}

void TriggerWorld::Remove(TriggerId id) {
  for (auto it = vols_.begin(); it != vols_.end(); ++it) {
    if (it->id == id) {
      vols_.erase(it);
      return;
    }
  }
}

TriggerVolume* TriggerWorld::Get(TriggerId id) {
  for (auto& v : vols_) {
    if (v.id == id) {
      return &v;
    }
  }
  return nullptr;
}

void TriggerWorld::Tick(engine::scene::World& world, EntityWorld& entities, EventBus& events,
                        ScriptComponentWorld& scripts) {
  for (auto& vol : vols_) {
    if (vol.node == engine::scene::kInvalidNode || !world.valid(vol.node)) {
      continue;
    }
    const auto center = world.local_transform(vol.node).position;
    std::vector<EntityId> now;
    for (const auto& e : entities.all()) {
      if (e.node == engine::scene::kInvalidNode || !world.valid(e.node)) {
        continue;
      }
      if (!vol.target_entity.empty() && e.name != vol.target_entity) {
        continue;
      }
      const auto p = world.local_transform(e.node).position;
      if (AabbContains(center, vol.half_extents, p)) {
        now.push_back(e.id);
      }
    }

    auto was_inside = [&](EntityId id) {
      for (EntityId old : vol.inside) {
        if (old == id) {
          return true;
        }
      }
      return false;
    };
    auto is_inside = [&](EntityId id) {
      for (EntityId n : now) {
        if (n == id) {
          return true;
        }
      }
      return false;
    };

    for (EntityId id : now) {
      if (was_inside(id)) {
        continue;
      }
      Entity* e = entities.Get(id);
      const std::string other = e ? e->name : std::to_string(id);
      std::ostringstream payload;
      payload << vol.name << ',' << other;
      events.Publish("trigger.enter", payload.str());
      scripts.DispatchTrigger(vol.node, true, other);
    }
    for (EntityId id : vol.inside) {
      if (is_inside(id)) {
        continue;
      }
      Entity* e = entities.Get(id);
      const std::string other = e ? e->name : std::to_string(id);
      std::ostringstream payload;
      payload << vol.name << ',' << other;
      events.Publish("trigger.leave", payload.str());
      scripts.DispatchTrigger(vol.node, false, other);
    }
    vol.inside = std::move(now);
  }
}

void TriggerWorld::Clear() {
  vols_.clear();
  next_id_ = 1;
}

}  // namespace game_kit
