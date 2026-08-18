#include "undo.h"

#include "editing/ops.h"
#include "play/scene_play.h"

namespace editor {
namespace {

void DestroySnapRoots(engine::scene::World& world, const std::vector<NodeSnap>& snaps) {
  for (const auto& s : snaps) {
    if (s.parent_index >= 0) {
      continue;
    }
    if (world.valid(s.live)) {
      (void)world.DestroyNode(s.live);
    }
  }
}

}  // namespace

void UndoStack::Push(engine::scene::NodeId node, const engine::scene::Transform& before,
                     const engine::scene::Transform& after) {
  PushBatch({node}, {before}, {after});
}

void UndoStack::PushBatch(const std::vector<engine::scene::NodeId>& nodes,
                          const std::vector<engine::scene::Transform>& before,
                          const std::vector<engine::scene::Transform>& after) {
  if (nodes.empty() || nodes.size() != before.size() || nodes.size() != after.size()) {
    return;
  }
  Cmd c;
  c.kind = Kind::Transform;
  c.nodes = nodes;
  c.before = before;
  c.after = after;
  undo_.push_back(std::move(c));
  redo_.clear();
}

void UndoStack::PushSpawn(std::vector<NodeSnap> snaps) {
  if (snaps.empty()) {
    return;
  }
  Cmd c;
  c.kind = Kind::Spawn;
  c.snaps = std::move(snaps);
  undo_.push_back(std::move(c));
  redo_.clear();
}

void UndoStack::PushKill(std::vector<NodeSnap> snaps) {
  if (snaps.empty()) {
    return;
  }
  Cmd c;
  c.kind = Kind::Kill;
  c.snaps = std::move(snaps);
  undo_.push_back(std::move(c));
  redo_.clear();
}

void UndoStack::PushProps(std::vector<PropSnap> before, std::vector<PropSnap> after) {
  if (before.empty() || before.size() != after.size()) {
    return;
  }
  Cmd c;
  c.kind = Kind::Props;
  c.prop_before = std::move(before);
  c.prop_after = std::move(after);
  undo_.push_back(std::move(c));
  redo_.clear();
}

void UndoStack::ApplySpawn(engine::scene::World& world, Cmd* c,
                           std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (!c) {
    return;
  }
  RestoreSnaps(world, &c->snaps, meta);
}

void UndoStack::ApplyKill(engine::scene::World& world, Cmd* c,
                          std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (!c) {
    return;
  }
  DestroySnapRoots(world, c->snaps);
  if (meta) {
    for (const auto& s : c->snaps) {
      meta->erase(s.live);
    }
  }
}

void UndoStack::ApplyProps(engine::scene::World& world, const std::vector<PropSnap>& props,
                           std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  for (const auto& p : props) {
    ApplyProp(world, p, meta);
  }
}

bool UndoStack::Undo(engine::scene::World& world,
                     std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (undo_.empty()) {
    return false;
  }
  Cmd c = std::move(undo_.back());
  undo_.pop_back();
  switch (c.kind) {
    case Kind::Transform:
      for (std::size_t i = 0; i < c.nodes.size(); ++i) {
        if (world.valid(c.nodes[i])) {
          world.set_local_transform(c.nodes[i], c.before[i]);
        }
      }
      break;
    case Kind::Spawn:
      ApplyKill(world, &c, meta);
      break;
    case Kind::Kill:
      ApplySpawn(world, &c, meta);
      break;
    case Kind::Props:
      ApplyProps(world, c.prop_before, meta);
      break;
  }
  redo_.push_back(std::move(c));
  return true;
}

bool UndoStack::Redo(engine::scene::World& world,
                     std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (redo_.empty()) {
    return false;
  }
  Cmd c = std::move(redo_.back());
  redo_.pop_back();
  switch (c.kind) {
    case Kind::Transform:
      for (std::size_t i = 0; i < c.nodes.size(); ++i) {
        if (world.valid(c.nodes[i])) {
          world.set_local_transform(c.nodes[i], c.after[i]);
        }
      }
      break;
    case Kind::Spawn:
      ApplySpawn(world, &c, meta);
      break;
    case Kind::Kill:
      ApplyKill(world, &c, meta);
      break;
    case Kind::Props:
      ApplyProps(world, c.prop_after, meta);
      break;
  }
  undo_.push_back(std::move(c));
  return true;
}

void UndoStack::Clear() {
  undo_.clear();
  redo_.clear();
}

}  // namespace editor
