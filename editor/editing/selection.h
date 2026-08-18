#pragma once

#include "engine/scene/world.h"

#include <algorithm>
#include <vector>

namespace editor {

struct Selection {
  engine::scene::NodeId node = engine::scene::kInvalidNode;
  std::vector<engine::scene::NodeId> extra;
  bool dragging = false;
  float drag_acc_x = 0.f;
  float drag_acc_z = 0.f;
  int gizmo_axis = 0;
  float axis_u0 = 0.f;
  bool plane_drag = false;
  engine::Vec3 plane0{};
  std::vector<engine::scene::Transform> drag_origins;

  [[nodiscard]] std::vector<engine::scene::NodeId> All() const {
    std::vector<engine::scene::NodeId> out;
    if (node != engine::scene::kInvalidNode) {
      out.push_back(node);
    }
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
  }

  [[nodiscard]] bool Contains(engine::scene::NodeId id) const {
    if (id == engine::scene::kInvalidNode) {
      return false;
    }
    if (node == id) {
      return true;
    }
    return std::find(extra.begin(), extra.end(), id) != extra.end();
  }

  void Clear() {
    node = engine::scene::kInvalidNode;
    extra.clear();
    dragging = false;
    drag_acc_x = 0.f;
    drag_acc_z = 0.f;
    gizmo_axis = 0;
    axis_u0 = 0.f;
    plane_drag = false;
    plane0 = {};
    drag_origins.clear();
  }

  void Set(engine::scene::NodeId id) {
    extra.clear();
    node = id;
    dragging = false;
    drag_acc_x = 0.f;
    drag_acc_z = 0.f;
    gizmo_axis = 0;
    axis_u0 = 0.f;
    plane_drag = false;
    plane0 = {};
    drag_origins.clear();
  }

  void Toggle(engine::scene::NodeId id) {
    if (id == engine::scene::kInvalidNode) {
      return;
    }
    if (node == id) {
      if (extra.empty()) {
        node = engine::scene::kInvalidNode;
      } else {
        node = extra.front();
        extra.erase(extra.begin());
      }
      return;
    }
    auto it = std::find(extra.begin(), extra.end(), id);
    if (it != extra.end()) {
      extra.erase(it);
      return;
    }
    if (node == engine::scene::kInvalidNode) {
      node = id;
    } else {
      extra.push_back(id);
    }
  }
};

}  // namespace editor
