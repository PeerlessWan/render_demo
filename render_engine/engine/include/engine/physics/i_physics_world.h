#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <memory>
#include <vector>

namespace engine::physics {

struct RayHit {
  bool hit = false;
  Vec3 point{};
  Vec3 normal{0, 1, 0};
  float distance = 0.f;
  int body_id = -1;
};

struct RigidBodyDesc {
  Vec3 position{};
  Vec3 half_extents{0.5f, 0.5f, 0.5f};
  float mass = 1.f;
  bool is_trigger = false;
};

class IPhysicsWorld {
 public:
  virtual ~IPhysicsWorld() = default;
  virtual int CreateBox(const RigidBodyDesc& desc) = 0;
  virtual void Step(float dt) = 0;
  virtual RayHit Raycast(const Vec3& origin, const Vec3& dir, float max_dist) const = 0;
  virtual Status MoveCharacter(int body_id, const Vec3& displacement) = 0;
  [[nodiscard]] virtual Vec3 body_position(int body_id) const = 0;
  [[nodiscard]] virtual const char* backend_name() const = 0;
};

std::unique_ptr<IPhysicsWorld> CreateBuiltinPhysicsWorld();

// Real Jolt adapter when ENGINE_WITH_JOLT=1; otherwise nullptr.
std::unique_ptr<IPhysicsWorld> CreateJoltPhysicsWorld();

// Prefers Jolt when available; otherwise builtin.
std::unique_ptr<IPhysicsWorld> CreateDefaultPhysicsWorld();

}  // namespace engine::physics
