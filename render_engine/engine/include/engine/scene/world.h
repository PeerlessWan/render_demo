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
  std::string material_id;  // PBR slot / AssetId; empty = mesh default
  Aabb local_bounds{Vec3{-0.5f, -0.5f, -0.5f}, Vec3{0.5f, 0.5f, 0.5f}};
  bool visible = true;
  bool never_cull = false;  // skip frustum cull (large ground / helpers)
};

// World-authoritative components (editor + CaptureWorld). Kind: 0 point, 1 spot, 2 directional.
struct LightComponent {
  int kind = 0;
  float range = 8.f;
  float intensity = 1.f;
  Vec3 color{1.f, 0.95f, 0.85f};
};

struct CameraComponent {
  bool active = false;
  float fovy_rad = 1.04719755f;
};

struct ColliderComponent {
  float hx = 0.5f;
  float hy = 0.5f;
  float hz = 0.5f;
};

struct SpriteComponent {
  std::string atlas_id = "tiles";
  int gid = 1;
  int sort_layer = 0;
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
  void clear_mesh(NodeId id);

  void set_light(NodeId id, LightComponent light);
  [[nodiscard]] const LightComponent* light(NodeId id) const;
  void clear_light(NodeId id);

  void set_camera(NodeId id, CameraComponent camera);
  [[nodiscard]] const CameraComponent* camera(NodeId id) const;
  void clear_camera(NodeId id);

  void set_collider(NodeId id, ColliderComponent collider);
  [[nodiscard]] const ColliderComponent* collider(NodeId id) const;
  void clear_collider(NodeId id);

  void set_sprite(NodeId id, SpriteComponent sprite);
  [[nodiscard]] const SpriteComponent* sprite(NodeId id) const;
  void clear_sprite(NodeId id);

  void set_visible(NodeId id, bool visible);
  [[nodiscard]] bool visible(NodeId id) const;

  [[nodiscard]] const std::vector<NodeId>& roots() const { return roots_; }
  [[nodiscard]] const std::vector<NodeId>& children(NodeId id) const;
  [[nodiscard]] NodeId parent(NodeId id) const;
  // Reparent. new_parent = kInvalidNode → root. Rejects cycles / invalid ids.
  Status set_parent(NodeId id, NodeId new_parent);

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
    LightComponent light{};
    bool has_light = false;
    CameraComponent camera{};
    bool has_camera = false;
    ColliderComponent collider{};
    bool has_collider = false;
    SpriteComponent sprite{};
    bool has_sprite = false;
  };

  void MarkDirty(NodeId id);
  void UpdateNode(NodeId id, const Mat4& parent_world);

  std::vector<Node> nodes_;
  std::vector<NodeId> roots_;
  std::vector<NodeId> free_list_;
};

}  // namespace engine::scene
