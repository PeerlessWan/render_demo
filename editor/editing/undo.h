#pragma once

#include "editing/anim_edit.h"
#include "play/scene_play.h"

#include "engine/scene/world.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

struct EditorSettings;

struct NodeSnap {
  engine::scene::NodeId live = engine::scene::kInvalidNode;
  int parent_index = -1;
  engine::scene::NodeId external_parent = engine::scene::kInvalidNode;
  std::string name;
  engine::scene::Transform transform{};
  bool visible = true;
  bool has_mesh = false;
  engine::scene::MeshRenderer mesh{};
  std::string prefab_id;
  std::string script_path;
};

struct PropSnap {
  engine::scene::NodeId id = engine::scene::kInvalidNode;
  std::string name;
  bool visible = true;
  bool has_mesh = false;
  engine::scene::MeshRenderer mesh{};
  std::string prefab_id;
  std::string script_path;
  std::string parent_name;
  engine::scene::NodeId parent_id = engine::scene::kInvalidNode;
  NodeMeta meta{};
  bool has_sprite = false;
  engine::scene::SpriteComponent sprite{};
};

class UndoStack {
 public:
  void Push(engine::scene::NodeId node, const engine::scene::Transform& before,
            const engine::scene::Transform& after);
  void PushBatch(const std::vector<engine::scene::NodeId>& nodes,
                 const std::vector<engine::scene::Transform>& before,
                 const std::vector<engine::scene::Transform>& after);
  void PushSpawn(std::vector<NodeSnap> snaps);
  void PushKill(std::vector<NodeSnap> snaps);
  void PushProps(std::vector<PropSnap> before, std::vector<PropSnap> after);
  void PushGrid(std::vector<float> heights_before, std::vector<int> tiles_before,
                AnimGraphEdit anim_before, std::vector<float> heights_after,
                std::vector<int> tiles_after, AnimGraphEdit anim_after);

  bool Undo(engine::scene::World& world,
            std::unordered_map<engine::scene::NodeId, NodeMeta>* meta = nullptr,
            EditorSettings* settings = nullptr);
  bool Redo(engine::scene::World& world,
            std::unordered_map<engine::scene::NodeId, NodeMeta>* meta = nullptr,
            EditorSettings* settings = nullptr);
  void Clear();
  [[nodiscard]] bool empty() const { return undo_.empty(); }

 private:
  enum class Kind { Transform, Spawn, Kill, Props, Grid };
  struct Cmd {
    Kind kind = Kind::Transform;
    std::vector<engine::scene::NodeId> nodes;
    std::vector<engine::scene::Transform> before;
    std::vector<engine::scene::Transform> after;
    std::vector<NodeSnap> snaps;
    std::vector<PropSnap> prop_before;
    std::vector<PropSnap> prop_after;
    std::vector<float> heights_before;
    std::vector<float> heights_after;
    std::vector<int> tiles_before;
    std::vector<int> tiles_after;
    AnimGraphEdit anim_before{};
    AnimGraphEdit anim_after{};
  };

  void ApplySpawn(engine::scene::World& world, Cmd* c,
                  std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);
  void ApplyKill(engine::scene::World& world, Cmd* c,
                 std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);
  void ApplyProps(engine::scene::World& world, const std::vector<PropSnap>& props,
                  std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

  std::vector<Cmd> undo_;
  std::vector<Cmd> redo_;
};

}  // namespace editor
