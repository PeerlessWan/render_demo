#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace engine::gameplay {

// Mega-W10 / ADR 0037: free camera vs possess walk/jump (CPU, unit-testable).
// Height via SampleHeight(x,z); no large character assets -- procedural capsule mesh.

using SampleHeightFn = std::function<float(float x, float z)>;

struct AabbObstacle {
  Vec3 min{};
  Vec3 max{};
};

struct PossessParams {
  float capsule_radius = 0.35f;
  float capsule_height = 1.8f;  // total height including hemispheres
  float walk_speed = 4.5f;
  float jump_speed = 6.5f;
  float gravity = 18.f;
  float ground_skin = 0.04f;
  // Third-person camera: offset in character yaw space (back + up).
  Vec3 camera_offset{-0.f, 1.55f, -3.2f};
};

struct PossessInput {
  float move_x = 0.f;  // strafe (-1..1), world XZ relative to yaw
  float move_z = 0.f;  // forward (-1..1)
  bool jump = false;
  float yaw = 0.f;  // radians; facing / camera orbit yaw
};

struct CapsuleCharacterMesh {
  std::vector<Vec3> positions;
  std::vector<std::uint32_t> indices;
};

// Procedural standing capsule (Y-up) centered at origin feet on y=0.
[[nodiscard]] CapsuleCharacterMesh BuildCapsuleCharacterMesh(float radius = 0.35f,
                                                             float height = 1.8f,
                                                             int rings = 8, int segments = 12);

class PossessController {
 public:
  PossessParams params{};

  // Default false -> free camera (host moves camera; Step is no-op for locomotion).
  bool possess_character = false;

  Vec3 position{0.f, 0.f, 0.f};  // feet / capsule bottom contact point
  Vec3 velocity{};
  bool on_ground = false;

  void SetSampleHeight(SampleHeightFn fn) { sample_height_ = std::move(fn); }
  void ClearObstacles() { obstacles_.clear(); }
  void AddObstacle(const AabbObstacle& box) { obstacles_.push_back(box); }

  // When possess_character is false, returns without changing position (free camera mode).
  void Step(float dt, const PossessInput& input);

  // Camera eye in world space from character pose + yaw (third-person helper).
  [[nodiscard]] Vec3 ThirdPersonCameraPosition(float yaw) const;
  [[nodiscard]] Vec3 ThirdPersonLookAt() const;

  [[nodiscard]] float CapsuleCenterY() const;
  [[nodiscard]] Vec3 CapsuleCenter() const;

 private:
  SampleHeightFn sample_height_;
  std::vector<AabbObstacle> obstacles_;

  [[nodiscard]] float GroundY(float x, float z) const;
  void ResolveAabbHorizontal();
  void StickToGround();
};

}  // namespace engine::gameplay
