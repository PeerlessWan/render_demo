#pragma once

namespace editor {

struct EditorSettings {
  bool snap = true;
  float grid = 1.f;
  bool show_grid = true;
  bool show_gizmo = true;
  bool dirty = false;
  int gizmo_mode = 0;         // 0 Move, 1 Rotate, 2 Scale
  int hierarchy_filter = 0;  // 0 All, 1 Mesh, 2 Empty
};

}  // namespace editor
