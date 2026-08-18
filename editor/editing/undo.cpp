#include "undo.h"

namespace editor {

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
  undo_.push_back(Cmd{nodes, before, after});
  redo_.clear();
}

bool UndoStack::Undo(engine::scene::World& world) {
  if (undo_.empty()) {
    return false;
  }
  Cmd c = undo_.back();
  undo_.pop_back();
  for (std::size_t i = 0; i < c.nodes.size(); ++i) {
    if (world.valid(c.nodes[i])) {
      world.set_local_transform(c.nodes[i], c.before[i]);
    }
  }
  redo_.push_back(std::move(c));
  return true;
}

bool UndoStack::Redo(engine::scene::World& world) {
  if (redo_.empty()) {
    return false;
  }
  Cmd c = redo_.back();
  redo_.pop_back();
  for (std::size_t i = 0; i < c.nodes.size(); ++i) {
    if (world.valid(c.nodes[i])) {
      world.set_local_transform(c.nodes[i], c.after[i]);
    }
  }
  undo_.push_back(std::move(c));
  return true;
}

void UndoStack::Clear() {
  undo_.clear();
  redo_.clear();
}

}  // namespace editor
