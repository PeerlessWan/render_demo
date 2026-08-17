#pragma once

#include "sim/blocks.h"
#include "world/world.h"

#include <vector>

namespace editor {

class VoxelUndo {
 public:
  void Push(int x, int y, int z, mc::Id before, mc::Id after);
  bool Undo(mc::World& world);
  bool Redo(mc::World& world);
  void Clear();

 private:
  struct Cmd {
    int x = 0;
    int y = 0;
    int z = 0;
    mc::Id before = mc::Id::Air;
    mc::Id after = mc::Id::Air;
  };
  std::vector<Cmd> undo_;
  std::vector<Cmd> redo_;
};

}  // namespace editor
