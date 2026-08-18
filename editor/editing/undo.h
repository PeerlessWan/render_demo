#pragma once

#include "engine/scene/world.h"

#include <vector>

namespace editor {

class UndoStack {
 public:
  void Push(engine::scene::NodeId node, const engine::scene::Transform& before,
            const engine::scene::Transform& after);
  void PushBatch(const std::vector<engine::scene::NodeId>& nodes,
                 const std::vector<engine::scene::Transform>& before,
                 const std::vector<engine::scene::Transform>& after);
  bool Undo(engine::scene::World& world);
  bool Redo(engine::scene::World& world);
  void Clear();
  [[nodiscard]] bool empty() const { return undo_.empty(); }

 private:
  struct Cmd {
    std::vector<engine::scene::NodeId> nodes;
    std::vector<engine::scene::Transform> before;
    std::vector<engine::scene::Transform> after;
  };
  std::vector<Cmd> undo_;
  std::vector<Cmd> redo_;
};

}  // namespace editor
