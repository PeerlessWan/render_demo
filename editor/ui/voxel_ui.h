#pragma once

#include "sim/blocks.h"
#include "world/world.h"

#include "engine/ui/immediate_ui.h"

#include <filesystem>

namespace editor {

struct VoxelEdit {
  bool enabled = false;
  mc::Id brush = mc::Id::Stone;
  int layer_y = 32;
  int brush_radius = 0;
  bool box_mode = false;
  bool box_has_a = false;
  int ax = 0;
  int ay = 0;
  int az = 0;
  bool dirty = false;
  int dir_slot = 0;
  std::filesystem::path dir{"worlds/slot0"};
};

struct VoxelCommands {
  bool save = false;
  bool load = false;
  bool play = false;
  bool pause = false;
  bool fill_layer = false;
  bool step = false;
};

void DrawVoxelUi(engine::ui::ImmediateUi& ui, VoxelEdit& edit, mc::World& world, VoxelCommands* cmd,
                 bool playing);

}  // namespace editor
