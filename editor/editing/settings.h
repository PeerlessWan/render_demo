#pragma once

#include <string>
#include <vector>

namespace editor {

struct EditorSettings {
  bool snap = true;
  float grid = 1.f;
  bool show_grid = true;
  bool show_gizmo = true;
  bool gizmo_local = false;
  bool show_bounds = false;
  bool show_profiler = false;
  bool show_collision = false;
  bool hot_reload = true;
  bool dirty = false;
  int gizmo_mode = 0;         // 0 Move, 1 Rotate, 2 Scale
  int hierarchy_filter = 0;  // 0 All, 1 Mesh, 2 Empty
  int viewport = 0;          // 0 Persp, 1 Top, 2 Front, 3 Side, 4 Node cam
  bool split_view = false;
  int active_pane = 0;
  int workspace = 0;  // 0 scene, 1 voxel
  char search[48]{};
  float sculpt = 0.25f;
  int tile_gid = 1;
  int tile_x = 8;
  int tile_y = 8;
  std::vector<float> heights;
  std::vector<int> tiles;
};

}  // namespace editor
