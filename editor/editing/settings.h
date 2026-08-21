#pragma once

#include "editing/anim_edit.h"

#include <string>
#include <vector>

namespace editor {

struct EditorSettings {
  bool snap = true;
  float grid = 1.f;
  bool show_grid = true;
  bool show_gizmo = true;
  bool gizmo_local = false;
  bool show_bounds = true;
  bool show_profiler = false;
  bool show_collision = false;
  bool show_output = true;  // W25: Godot-like Output dock
  bool hot_reload = true;
  bool dirty = false;
  int gizmo_mode = 0;         // 0 Move, 1 Rotate, 2 Scale
  int hierarchy_filter = 0;  // 0 All, 1 Mesh, 2 Empty
  int viewport = 0;          // 0 Persp, 1 Top, 2 Front, 3 Side, 4 Node cam, 5 2D ortho
  bool split_view = false;
  int active_pane = 0;
  int workspace = 0;  // 0 scene, 1 voxel
  char search[48]{};
  float sculpt = 0.25f;
  float brush_radius = 2.f;
  int sculpt_mode = 0;  // 0 Raise, 1 Lower, 2 Smooth
  int brush_tool = 0;   // 0 Select, 1 Sculpt, 2 TilePaint
  bool cook_on_save = false;
  int tile_gid = 1;
  int tile_x = 8;
  int tile_y = 8;
  std::string tile_atlas = "tiles";
  std::vector<float> heights;
  std::vector<int> tiles;
  AnimGraphEdit anim;
  bool anim_drive = false;  // preview: SampleCurve → selected node Y
  // W25: status / output feedback (Godot-like)
  std::string status_line = "就绪";
  std::vector<std::string> output_lines;
};

}  // namespace editor
