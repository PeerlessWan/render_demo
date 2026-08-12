#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::scene {

using NodeId = std::uint32_t;
inline constexpr NodeId kInvalidNode = static_cast<NodeId>(-1);

struct Transform {
  Vec3 position{};
  Quat rotation = Quat::Identity();
  Vec3 scale{1.f, 1.f, 1.f};
};

struct MeshRenderer {
  std::string mesh_id;
  Aabb local_bounds{Vec3{-0.5f, -0.5f, -0.5f}, Vec3{0.5f, 0.5f, 0.5f}};
  bool visible = true;
};

class World {
 public:
  NodeId CreateNode(std::string name = {}, NodeId parent = kInvalidNode);
  Status DestroyNode(NodeId id);

  [[nodiscard]] bool valid(NodeId id) const;
  [[nodiscard]] const std::string& name(NodeId id) const;
  void set_name(NodeId id, std::string name);

  void set_local_transform(NodeId id, const Transform& t);
  [[nodiscard]] const Transform& local_transform(NodeId id) const;
  [[nodiscard]] const Mat4& world_matrix(NodeId id) const;

  void set_mesh(NodeId id, MeshRenderer mesh);
  [[nodiscard]] const MeshRenderer* mesh(NodeId id) const;

  void set_visible(NodeId id, bool visible);
  [[nodiscard]] bool visible(NodeId id) const;

  [[nodiscard]] const std::vector<NodeId>& roots() const { return roots_; }
  [[nodiscard]] const std::vector<NodeId>& children(NodeId id) const;

  // Force recompute dirty world matrices.
  void UpdateTransforms();

 private:
  struct Node {
    std::string name;
    NodeId parent = kInvalidNode;
    std::vector<NodeId> children;
    Transform local{};
    Mat4 world = Mat4::Identity();
    bool dirty = true;
    bool visible = true;
    bool alive = true;
    MeshRenderer mesh{};
    bool has_mesh = false;
  };

  void MarkDirty(NodeId id);
  void UpdateNode(NodeId id, const Mat4& parent_world);

  std::vector<Node> nodes_;
  std::vector<NodeId> roots_;
  std::vector<NodeId> free_list_;
};

}  // namespace engine::scene
