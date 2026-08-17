#pragma once

#include "engine/scene/world.h"

namespace editor {

struct Selection {
  engine::scene::NodeId node = engine::scene::kInvalidNode;
  bool dragging = false;
};

}  // namespace editor
