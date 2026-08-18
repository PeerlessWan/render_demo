#include "game_kit/trigger.h"

#include "game_kit/event_bus.h"
#include "game_kit/script_component.h"

#include "engine/physics/i_physics_world.h"

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
      if (!e.active || e.node == engine::scene::kInvalidNode || !world.valid(e.node)) {
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

void TriggerWorld::TickPhysics(engine::physics::IPhysicsWorld& phys, EntityWorld& entities,
                               EventBus& events, ScriptComponentWorld& scripts) {
  std::vector<PhysPair> now;
  const auto contacts = phys.ConsumeContacts();
  const auto& all = entities.all();
  auto find_body = [&](int body) -> const Entity* {
    for (const auto& e : all) {
      if (e.active && e.physics_body == body) {
        return &e;
      }
    }
    return nullptr;
  };
  if (!contacts.empty()) {
    for (const auto& c : contacts) {
      const Entity* ea = find_body(c.a);
      const Entity* eb = find_body(c.b);
      if (!ea || !eb) {
        continue;
      }
      now.push_back(PhysPair{ea->id, eb->id});
    }
  } else {
    for (const auto& a : all) {
      if (!a.active || a.physics_body < 0 || !a.physics_is_trigger) {
        continue;
      }
      const auto hits =
          phys.OverlapAabb(phys.body_position(a.physics_body), phys.body_half_extents(a.physics_body));
      for (int bid : hits) {
        if (bid == a.physics_body) {
          continue;
        }
        const Entity* eb = find_body(bid);
        if (!eb) {
          continue;
        }
        now.push_back(PhysPair{a.id, eb->id});
      }
    }
  }

  auto has = [](const std::vector<PhysPair>& xs, EntityId a, EntityId b) {
    for (const auto& p : xs) {
      if (p.a == a && p.b == b) {
        return true;
      }
    }
    return false;
  };

  for (const auto& p : now) {
    if (has(phys_inside_, p.a, p.b)) {
      continue;
    }
    Entity* ta = entities.Get(p.a);
    Entity* tb = entities.Get(p.b);
    const std::string other = tb ? tb->name : "?";
    events.Publish("collision.enter", other);
    if (ta) {
      scripts.DispatchCollision(ta->node, true, other);
    }
  }
  for (const auto& p : phys_inside_) {
    if (has(now, p.a, p.b)) {
      continue;
    }
    Entity* ta = entities.Get(p.a);
    Entity* tb = entities.Get(p.b);
    const std::string other = tb ? tb->name : "?";
    events.Publish("collision.leave", other);
    if (ta) {
      scripts.DispatchCollision(ta->node, false, other);
    }
  }
  phys_inside_ = std::move(now);
}

void TriggerWorld::Clear() {
  vols_.clear();
  phys_inside_.clear();
  next_id_ = 1;
}

}  // namespace game_kit
