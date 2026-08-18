#include "game_kit/entity.h"

namespace game_kit {

EntityId EntityWorld::Create(std::string name, engine::scene::NodeId node) {
  Entity e;
  e.id = next_id_++;
  e.node = node;
  e.name = std::move(name);
  entities_.push_back(std::move(e));
  return entities_.back().id;
}

void EntityWorld::Destroy(EntityId id) {
  for (auto it = entities_.begin(); it != entities_.end(); ++it) {
    if (it->id == id) {
      entities_.erase(it);
      return;
    }
  }
}

Entity* EntityWorld::Get(EntityId id) {
  for (auto& e : entities_) {
    if (e.id == id) {
      return &e;
    }
  }
  return nullptr;
}

const Entity* EntityWorld::Get(EntityId id) const {
  for (const auto& e : entities_) {
    if (e.id == id) {
      return &e;
    }
  }
  return nullptr;
}

Entity* EntityWorld::FindByName(std::string_view name) {
  for (auto& e : entities_) {
    if (e.name == name) {
      return &e;
    }
  }
  return nullptr;
}

Entity* EntityWorld::FindByNode(engine::scene::NodeId node) {
  for (auto& e : entities_) {
    if (e.node == node) {
      return &e;
    }
  }
  return nullptr;
}

void EntityWorld::Clear() { entities_.clear(); }

}  // namespace game_kit
