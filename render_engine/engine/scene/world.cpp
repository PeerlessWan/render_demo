#include "engine/scene/world.h"

#include <algorithm>

namespace engine::scene {

NodeId World::CreateNode(std::string name, NodeId parent) {
  NodeId id = kInvalidNode;
  if (!free_list_.empty()) {
    id = free_list_.back();
    free_list_.pop_back();
    nodes_[id] = Node{};
  } else {
    id = static_cast<NodeId>(nodes_.size());
    nodes_.emplace_back();
  }
  auto& n = nodes_[id];
  n.alive = true;
  n.name = std::move(name);
  n.parent = parent;
  n.dirty = true;
  if (parent == kInvalidNode) {
    roots_.push_back(id);
  } else if (valid(parent)) {
    nodes_[parent].children.push_back(id);
    MarkDirty(id);
  }
  return id;
}

Status World::DestroyNode(NodeId id) {
  if (!valid(id)) {
    return Status::Fail(ErrorCode::NotFound, "node not found");
  }
  // Destroy children first (copy list).
  const auto kids = nodes_[id].children;
  for (NodeId c : kids) {
    DestroyNode(c);
  }
  auto& n = nodes_[id];
  if (n.parent == kInvalidNode) {
    roots_.erase(std::remove(roots_.begin(), roots_.end(), id), roots_.end());
  } else if (valid(n.parent)) {
    auto& sib = nodes_[n.parent].children;
    sib.erase(std::remove(sib.begin(), sib.end(), id), sib.end());
  }
  n = Node{};
  n.alive = false;
  free_list_.push_back(id);
  return Status::Ok();
}

bool World::valid(NodeId id) const {
  return id < nodes_.size() && nodes_[id].alive;
}

const std::string& World::name(NodeId id) const {
  static const std::string kEmpty;
  return valid(id) ? nodes_[id].name : kEmpty;
}

void World::set_name(NodeId id, std::string name) {
  if (valid(id)) {
    nodes_[id].name = std::move(name);
  }
}

void World::set_local_transform(NodeId id, const Transform& t) {
  if (!valid(id)) {
    return;
  }
  nodes_[id].local = t;
  MarkDirty(id);
}

const Transform& World::local_transform(NodeId id) const {
  static const Transform kIdentity;
  return valid(id) ? nodes_[id].local : kIdentity;
}

const Mat4& World::world_matrix(NodeId id) const {
  static const Mat4 kIdentity = Mat4::Identity();
  return valid(id) ? nodes_[id].world : kIdentity;
}

void World::set_mesh(NodeId id, MeshRenderer mesh) {
  if (!valid(id)) {
    return;
  }
  nodes_[id].mesh = std::move(mesh);
  nodes_[id].has_mesh = true;
}

const MeshRenderer* World::mesh(NodeId id) const {
  if (!valid(id) || !nodes_[id].has_mesh) {
    return nullptr;
  }
  return &nodes_[id].mesh;
}

void World::clear_mesh(NodeId id) {
  if (valid(id)) {
    nodes_[id].has_mesh = false;
    nodes_[id].mesh = MeshRenderer{};
  }
}

void World::set_light(NodeId id, LightComponent light) {
  if (!valid(id)) {
    return;
  }
  nodes_[id].light = light;
  nodes_[id].has_light = true;
}

const LightComponent* World::light(NodeId id) const {
  if (!valid(id) || !nodes_[id].has_light) {
    return nullptr;
  }
  return &nodes_[id].light;
}

void World::clear_light(NodeId id) {
  if (valid(id)) {
    nodes_[id].has_light = false;
    nodes_[id].light = LightComponent{};
  }
}

void World::set_camera(NodeId id, CameraComponent camera) {
  if (!valid(id)) {
    return;
  }
  nodes_[id].camera = camera;
  nodes_[id].has_camera = true;
}

const CameraComponent* World::camera(NodeId id) const {
  if (!valid(id) || !nodes_[id].has_camera) {
    return nullptr;
  }
  return &nodes_[id].camera;
}

void World::clear_camera(NodeId id) {
  if (valid(id)) {
    nodes_[id].has_camera = false;
    nodes_[id].camera = CameraComponent{};
  }
}

void World::set_collider(NodeId id, ColliderComponent collider) {
  if (!valid(id)) {
    return;
  }
  nodes_[id].collider = collider;
  nodes_[id].has_collider = true;
}

const ColliderComponent* World::collider(NodeId id) const {
  if (!valid(id) || !nodes_[id].has_collider) {
    return nullptr;
  }
  return &nodes_[id].collider;
}

void World::clear_collider(NodeId id) {
  if (valid(id)) {
    nodes_[id].has_collider = false;
    nodes_[id].collider = ColliderComponent{};
  }
}

void World::set_sprite(NodeId id, SpriteComponent sprite) {
  if (!valid(id)) {
    return;
  }
  nodes_[id].sprite = std::move(sprite);
  nodes_[id].has_sprite = true;
}

const SpriteComponent* World::sprite(NodeId id) const {
  if (!valid(id) || !nodes_[id].has_sprite) {
    return nullptr;
  }
  return &nodes_[id].sprite;
}

void World::clear_sprite(NodeId id) {
  if (valid(id)) {
    nodes_[id].has_sprite = false;
    nodes_[id].sprite = SpriteComponent{};
  }
}

void World::set_visible(NodeId id, bool visible) {
  if (valid(id)) {
    nodes_[id].visible = visible;
  }
}

bool World::visible(NodeId id) const { return valid(id) && nodes_[id].visible; }

NodeId World::parent(NodeId id) const {
  return valid(id) ? nodes_[id].parent : kInvalidNode;
}

Status World::set_parent(NodeId id, NodeId new_parent) {
  if (!valid(id)) {
    return Status::Fail(ErrorCode::NotFound, "node not found");
  }
  if (new_parent != kInvalidNode && !valid(new_parent)) {
    return Status::Fail(ErrorCode::InvalidArgument, "parent not found");
  }
  if (id == new_parent) {
    return Status::Fail(ErrorCode::InvalidArgument, "cannot parent to self");
  }
  for (NodeId walk = new_parent; valid(walk); walk = nodes_[walk].parent) {
    if (walk == id) {
      return Status::Fail(ErrorCode::InvalidArgument, "parent cycle");
    }
  }
  auto& n = nodes_[id];
  const NodeId old = n.parent;
  if (old == new_parent) {
    return Status::Ok();
  }
  if (old == kInvalidNode) {
    roots_.erase(std::remove(roots_.begin(), roots_.end(), id), roots_.end());
  } else if (valid(old)) {
    auto& sib = nodes_[old].children;
    sib.erase(std::remove(sib.begin(), sib.end(), id), sib.end());
  }
  n.parent = new_parent;
  if (new_parent == kInvalidNode) {
    roots_.push_back(id);
  } else {
    nodes_[new_parent].children.push_back(id);
  }
  MarkDirty(id);
  return Status::Ok();
}

const std::vector<NodeId>& World::children(NodeId id) const {
  static const std::vector<NodeId> kEmpty;
  return valid(id) ? nodes_[id].children : kEmpty;
}

void World::MarkDirty(NodeId id) {
  if (!valid(id)) {
    return;
  }
  nodes_[id].dirty = true;
  for (NodeId c : nodes_[id].children) {
    MarkDirty(c);
  }
}

void World::UpdateNode(NodeId id, const Mat4& parent_world) {
  auto& n = nodes_[id];
  if (n.dirty) {
    n.world = parent_world * Mat4::TRS(n.local.position, n.local.rotation, n.local.scale);
    n.dirty = false;
  }
  for (NodeId c : n.children) {
    UpdateNode(c, n.world);
  }
}

void World::UpdateTransforms() {
  const Mat4 identity = Mat4::Identity();
  for (NodeId r : roots_) {
    if (valid(r)) {
      UpdateNode(r, identity);
    }
  }
}

}  // namespace engine::scene
