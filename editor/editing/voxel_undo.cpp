#include "editing/voxel_undo.h"

#include "world/world.h"

namespace editor {

void VoxelUndo::BeginGroup() {
  grouping_ = true;
  open_ = {};
}

void VoxelUndo::EndGroup() {
  grouping_ = false;
  if (open_.cells.empty()) {
    return;
  }
  undo_.push_back(std::move(open_));
  open_ = {};
  redo_.clear();
}

void VoxelUndo::Push(int x, int y, int z, mc::Id before, mc::Id after) {
  if (before == after) {
    return;
  }
  if (grouping_) {
    open_.cells.push_back(Cell{x, y, z, before, after});
    return;
  }
  undo_.push_back(Cmd{{{x, y, z, before, after}}});
  redo_.clear();
}

bool VoxelUndo::Undo(mc::World& world) {
  if (undo_.empty()) {
    return false;
  }
  Cmd c = undo_.back();
  undo_.pop_back();
  for (auto it = c.cells.rbegin(); it != c.cells.rend(); ++it) {
    world.Set(it->x, it->y, it->z, it->before);
  }
  redo_.push_back(std::move(c));
  return true;
}

bool VoxelUndo::Redo(mc::World& world) {
  if (redo_.empty()) {
    return false;
  }
  Cmd c = redo_.back();
  redo_.pop_back();
  for (const auto& cell : c.cells) {
    world.Set(cell.x, cell.y, cell.z, cell.after);
  }
  undo_.push_back(std::move(c));
  return true;
}

void VoxelUndo::Clear() {
  undo_.clear();
  redo_.clear();
  open_ = {};
  grouping_ = false;
}

}  // namespace editor
