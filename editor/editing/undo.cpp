#include "undo.h"

namespace editor {

void UndoStack::Push(engine::scene::NodeId node, const engine::scene::Transform& before,
                     const engine::scene::Transform& after) {
  undo_.push_back(Cmd{node, before, after});
  redo_.clear();
}

bool UndoStack::Undo(engine::scene::World& world) {
  if (undo_.empty()) {
    return false;
  }
  Cmd c = undo_.back();
  undo_.pop_back();
  if (world.valid(c.node)) {
    world.set_local_transform(c.node, c.before);
  }
  redo_.push_back(c);
  return true;
}

bool UndoStack::Redo(engine::scene::World& world) {
  if (redo_.empty()) {
    return false;
  }
  Cmd c = redo_.back();
  redo_.pop_back();
  if (world.valid(c.node)) {
    world.set_local_transform(c.node, c.after);
  }
  undo_.push_back(c);
  return true;
}

void UndoStack::Clear() {
  undo_.clear();
  redo_.clear();
}

}  // namespace editor
