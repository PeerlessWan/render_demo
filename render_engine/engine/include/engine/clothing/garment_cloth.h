#pragma once

#include "engine/core/math.h"
#include "engine/physics/i_physics_world.h"

#include <cstdint>
#include <vector>

namespace engine::clothing {

// Mega-W10 / ADR 0037: demo cape/skirt SoftBody (not a DCC clothing pipeline).
// Prefer CPU Verlet/spring; optionally mirror a thin IPhysicsWorld SoftBody cube when available.

enum class GarmentKind { Cape, Skirt };

struct GarmentMeshDesc {
  GarmentKind kind = GarmentKind::Cape;
  int rows = 6;
  int cols = 5;
  float width = 0.9f;
  float length = 1.1f;
};

struct CapsuleCollider {
  Vec3 center{};
  float radius = 0.35f;
  float half_height = 0.55f;  // cylinder half-height between hemispheres
};

struct GarmentCloth {
  std::vector<Vec3> positions;
  std::vector<Vec3> prev_positions;
  std::vector<std::uint32_t> indices;
  std::vector<int> pinned;  // vertex indices fixed to attach points
  std::vector<Vec3> pin_targets;

  float gravity = 9.8f;
  float damping = 0.98f;
  float stretch_stiffness = 0.55f;
  int solver_iterations = 4;

  // Optional Jolt/thin SoftBody path (-1 = CPU Verlet only).
  int soft_body_id = -1;

  void Generate(const GarmentMeshDesc& desc, const Vec3& origin);
  // Cape: pin top row; Skirt: pin top ring/row. Targets updated each frame.
  void SetAttachPoints(const std::vector<Vec3>& world_points);

  // Try CreateSoftBody cube; on success soft_body_id >= 0 (caller must Step the world).
  // Cape/skirt Verlet remains authoritative for pin + capsule demo.
  bool TryWirePhysicsSoftBody(physics::IPhysicsWorld& world, const Vec3& position);

  // Verlet/spring step + pin lock + capsule collide. Safe when soft_body_id set (CPU still runs).
  void Step(float dt, const CapsuleCollider* capsule);

  // If soft_body_id valid, overwrite positions from physics (demo cube sync).
  bool SyncFromPhysics(physics::IPhysicsWorld& world);

  [[nodiscard]] bool AllFinite() const;

 private:
  void ApplyPins();

  int grid_cols_ = 0;
  int grid_rows_ = 0;
  float cell_w_ = 0.2f;
  float cell_h_ = 0.2f;
};

}  // namespace engine::clothing
