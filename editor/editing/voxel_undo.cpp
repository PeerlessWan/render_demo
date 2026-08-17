#include "editing/voxel_undo.h"

#include "world/world.h"

namespace editor {

void VoxelUndo::Push(int x, int y, int z, mc::Id before, mc::Id after) {
  undo_.push_back(Cmd{x, y, z, before, after});
  redo_.clear();
}

bool VoxelUndo::Undo(mc::World& world) {
  if (undo_.empty()) {
    return false;
  }
  Cmd c = undo_.back();
  undo_.pop_back();
  world.Set(c.x, c.y, c.z, c.before);
  redo_.push_back(c);
  return true;
}

bool VoxelUndo::Redo(mc::World& world) {
  if (redo_.empty()) {
    return false;
  }
  Cmd c = redo_.back();
  redo_.pop_back();
  world.Set(c.x, c.y, c.z, c.after);
  undo_.push_back(c);
  return true;
}

void VoxelUndo::Clear() {
  undo_.clear();
  redo_.clear();
}

}  // namespace editor
