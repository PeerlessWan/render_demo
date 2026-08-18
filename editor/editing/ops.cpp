#include "ops.h"

#include "play/scene_play.h"

#include "engine/core/math.h"

#include <unordered_map>

namespace editor {

engine::scene::NodeId FindParent(const engine::scene::World& world, engine::scene::NodeId id) {
  return world.parent(id);
}

void CaptureNode(const engine::scene::World& world, engine::scene::NodeId id,
                 const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta, NodeSnap* out) {
  if (!out || !world.valid(id)) {
    return;
  }
  out->live = id;
  out->name = world.name(id);
  out->transform = world.local_transform(id);
  out->visible = world.visible(id);
  if (const auto* mesh = world.mesh(id)) {
    out->has_mesh = true;
    out->mesh = *mesh;
  }
  auto it = meta.find(id);
  if (it != meta.end()) {
    out->prefab_id = it->second.prefab_id;
    out->script_path = it->second.script_path;
  }
}

void CaptureSubtree(const engine::scene::World& world, engine::scene::NodeId root,
                    const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta,
                    std::vector<NodeSnap>* out) {
  if (!out || !world.valid(root)) {
    return;
  }
  std::vector<engine::scene::NodeId> sub;
  std::vector<engine::scene::NodeId> stack{root};
  std::unordered_map<engine::scene::NodeId, char> in_sub;
  while (!stack.empty()) {
    const auto id = stack.back();
    stack.pop_back();
    if (!world.valid(id) || in_sub[id]) {
      continue;
    }
    in_sub[id] = 1;
    sub.push_back(id);
    for (auto c : world.children(id)) {
      stack.push_back(c);
    }
  }
  std::unordered_map<engine::scene::NodeId, int> index;
  for (auto id : sub) {
    NodeSnap s;
    CaptureNode(world, id, meta, &s);
    const auto p = FindParent(world, id);
    s.external_parent = p;
    s.parent_index = -1;
    if (index.count(p)) {
      s.parent_index = index[p];
    }
    index[id] = static_cast<int>(out->size());
    out->push_back(std::move(s));
  }
}

void RestoreSnaps(engine::scene::World& world, std::vector<NodeSnap>* snaps,
                  std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (!snaps) {
    return;
  }
  for (auto& s : *snaps) {
    engine::scene::NodeId parent = engine::scene::kInvalidNode;
    if (s.parent_index >= 0 && s.parent_index < static_cast<int>(snaps->size())) {
      parent = (*snaps)[static_cast<std::size_t>(s.parent_index)].live;
    } else if (world.valid(s.external_parent)) {
      parent = s.external_parent;
    }
    const auto id = world.CreateNode(s.name, parent);
    s.live = id;
    world.set_local_transform(id, s.transform);
    world.set_visible(id, s.visible);
    if (s.has_mesh) {
      world.set_mesh(id, s.mesh);
    }
    if (meta) {
      (*meta)[id] = NodeMeta{s.prefab_id, s.script_path};
    }
  }
}

void ApplyProp(engine::scene::World& world, const PropSnap& p,
               std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (!world.valid(p.id)) {
    return;
  }
  world.set_name(p.id, p.name);
  world.set_visible(p.id, p.visible);
  if (p.has_mesh) {
    world.set_mesh(p.id, p.mesh);
  } else {
    world.clear_mesh(p.id);
  }
  if (p.has_sprite) {
    world.set_sprite(p.id, p.sprite);
  } else {
    world.clear_sprite(p.id);
  }
  engine::scene::NodeId parent = engine::scene::kInvalidNode;
  if (world.valid(p.parent_id)) {
    parent = p.parent_id;
  } else if (!p.parent_name.empty()) {
    std::vector<engine::scene::NodeId> all;
    std::vector<engine::scene::NodeId> stack = world.roots();
    while (!stack.empty()) {
      auto n = stack.back();
      stack.pop_back();
      if (!world.valid(n)) {
        continue;
      }
      if (world.name(n) == p.parent_name) {
        parent = n;
        break;
      }
      for (auto c : world.children(n)) {
        stack.push_back(c);
      }
    }
  }
  (void)world.set_parent(p.id, parent);
  if (meta) {
    (*meta)[p.id] = p.meta;
    (*meta)[p.id].prefab_id = p.prefab_id.empty() ? p.meta.prefab_id : p.prefab_id;
    (*meta)[p.id].script_path = p.script_path.empty() ? p.meta.script_path : p.script_path;
    SyncMetaToWorld(world, *meta);
  }
}

PropSnap CaptureProp(const engine::scene::World& world, engine::scene::NodeId id,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta) {
  PropSnap p;
  p.id = id;
  if (!world.valid(id)) {
    return p;
  }
  p.name = world.name(id);
  p.visible = world.visible(id);
  const auto parent = world.parent(id);
  if (world.valid(parent)) {
    p.parent_name = world.name(parent);
    p.parent_id = parent;
  }
  if (const auto* mesh = world.mesh(id)) {
    p.has_mesh = true;
    p.mesh = *mesh;
  }
  if (const auto* spr = world.sprite(id)) {
    p.has_sprite = true;
    p.sprite = *spr;
  }
  auto it = meta.find(id);
  if (it != meta.end()) {
    p.prefab_id = it->second.prefab_id;
    p.script_path = it->second.script_path;
    p.meta = it->second;
  }
  return p;
}

engine::scene::NodeId DuplicateNode(engine::scene::World& world, engine::scene::NodeId src,
                                    float offset) {
  if (!world.valid(src)) {
    return engine::scene::kInvalidNode;
  }
  const std::string name = world.name(src).empty() ? "copy" : world.name(src) + "_copy";
  const auto id = world.CreateNode(name, world.parent(src));
  auto t = world.local_transform(src);
  t.position.x += offset;
  world.set_local_transform(id, t);
  if (const auto* mesh = world.mesh(src)) {
    world.set_mesh(id, *mesh);
  }
  if (const auto* L = world.light(src)) {
    world.set_light(id, *L);
  }
  if (const auto* cam = world.camera(src)) {
    world.set_camera(id, *cam);
  }
  if (const auto* col = world.collider(src)) {
    world.set_collider(id, *col);
  }
  if (const auto* spr = world.sprite(src)) {
    world.set_sprite(id, *spr);
  }
  world.set_visible(id, world.visible(src));
  return id;
}

std::vector<engine::scene::NodeId> DuplicateSelection(engine::scene::World& world, const Selection& sel,
                                                      float offset) {
  std::vector<engine::scene::NodeId> created;
  for (auto id : sel.All()) {
    const auto copy = DuplicateNode(world, id, offset);
    if (copy != engine::scene::kInvalidNode) {
      created.push_back(copy);
    }
  }
  return created;
}

void DestroySelection(engine::scene::World& world, Selection* sel) {
  if (!sel) {
    return;
  }
  for (auto id : sel->All()) {
    (void)world.DestroyNode(id);
  }
  sel->Clear();
}

void FrameCamera(engine::render::Camera* cam, const engine::scene::World& world,
                 engine::scene::NodeId node) {
  if (!cam || !world.valid(node)) {
    return;
  }
  const auto p = world.local_transform(node).position;
  cam->position = p + engine::Vec3{0.f, 2.5f, 6.f};
  cam->yaw = 0.f;
  cam->pitch = -0.25f;
}

}  // namespace editor
