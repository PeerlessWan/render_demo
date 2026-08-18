#include "game_kit/entity.h"

namespace game_kit {

void Entity::AddTag(std::string tag) {
  if (!HasTag(tag)) {
    tags.push_back(std::move(tag));
  }
}

bool Entity::HasTag(std::string_view tag) const {
  for (const auto& t : tags) {
    if (t == tag) {
      return true;
    }
  }
  return false;
}

EntityId EntityWorld::Create(std::string name, engine::scene::NodeId node) {
  Entity e;
  e.id = next_id_++;
  e.node = node;
  e.name = std::move(name);
  e.active = true;
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

void EntityWorld::Deactivate(EntityId id) {
  if (auto* e = Get(id)) {
    e->active = false;
  }
}

EntityId EntityWorld::Acquire(std::string name, engine::scene::NodeId node) {
  for (auto& e : entities_) {
    if (!e.active && e.name == name) {
      e.active = true;
      e.node = node;
      e.script_frozen = false;
      return e.id;
    }
  }
  return Create(std::move(name), node);
}

void EntityWorld::Release(EntityId id) { Deactivate(id); }

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
  return const_cast<Entity*>(static_cast<const EntityWorld*>(this)->FindByName(name));
}

const Entity* EntityWorld::FindByName(std::string_view name) const {
  for (const auto& e : entities_) {
    if (e.active && e.name == name) {
      return &e;
    }
  }
  return nullptr;
}

Entity* EntityWorld::FindByNode(engine::scene::NodeId node) {
  return const_cast<Entity*>(static_cast<const EntityWorld*>(this)->FindByNode(node));
}

const Entity* EntityWorld::FindByNode(engine::scene::NodeId node) const {
  for (const auto& e : entities_) {
    if (e.active && e.node == node) {
      return &e;
    }
  }
  return nullptr;
}

std::vector<Entity*> EntityWorld::FindByTag(std::string_view tag) {
  std::vector<Entity*> out;
  for (auto& e : entities_) {
    if (e.active && e.HasTag(tag)) {
      out.push_back(&e);
    }
  }
  return out;
}

void EntityWorld::Clear() { entities_.clear(); }

}  // namespace game_kit
