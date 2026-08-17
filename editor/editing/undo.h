#pragma once

#include "engine/scene/world.h"

#include <vector>

namespace editor {

class UndoStack {
 public:
  void Push(engine::scene::NodeId node, const engine::scene::Transform& before,
            const engine::scene::Transform& after);
  bool Undo(engine::scene::World& world);
  bool Redo(engine::scene::World& world);
  void Clear();

 private:
  struct Cmd {
    engine::scene::NodeId node = engine::scene::kInvalidNode;
    engine::scene::Transform before{};
    engine::scene::Transform after{};
  };
  std::vector<Cmd> undo_;
  std::vector<Cmd> redo_;
};

}  // namespace editor
