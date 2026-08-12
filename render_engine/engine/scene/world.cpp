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

void World::set_visible(NodeId id, bool visible) {
  if (valid(id)) {
    nodes_[id].visible = visible;
  }
}

bool World::visible(NodeId id) const { return valid(id) && nodes_[id].visible; }

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
