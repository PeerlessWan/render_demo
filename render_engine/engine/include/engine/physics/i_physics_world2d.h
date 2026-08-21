#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine::physics {

struct RayHit2D {
  bool hit = false;
  Vec2 point{};
  Vec2 normal{0.f, -1.f};
  float distance = 0.f;
  int body_id = -1;
};

struct ContactPair2D {
  int a = -1;
  int b = -1;
};

struct AreaEvent2D {
  int area_id = -1;
  int other_id = -1;
  bool entered = true;  // false = exited
};

enum class BodyType2D : std::uint8_t { Static = 0, Rigid = 1, Character = 2, Area = 3 };

enum class ShapeType2D : std::uint8_t { Rectangle = 0, Circle = 1, Capsule = 2 };

struct Shape2DDesc {
  ShapeType2D type = ShapeType2D::Rectangle;
  Vec2 half_extents{0.5f, 0.5f};  // rect; capsule uses x=radius, y=half_height
  float radius = 0.5f;            // circle / capsule
};

struct Body2DDesc {
  Vec2 position{};
  Shape2DDesc shape{};
  float mass = 1.f;
  float linear_damping = 0.f;
  float angular_damping = 0.f;
  std::uint32_t collision_layer = 1u;
  std::uint32_t collision_mask = 0xFFFFFFFFu;
  bool is_trigger = false;  // Area always trigger
};

enum class JointType2D : std::uint8_t { Pin = 0, Hinge = 1 };

struct Joint2DDesc {
  JointType2D type = JointType2D::Pin;
  int body_a = -1;
  int body_b = -1;
  Vec2 anchor_a{};
  Vec2 anchor_b{};
};

struct CharacterMove2DParams {
  float max_slope_deg = 45.f;
  float floor_max_angle = 0.785398f;  // ~45 deg
  float snap_length = 0.2f;
};

class IPhysicsWorld2D {
 public:
  virtual ~IPhysicsWorld2D() = default;

  virtual int CreateStaticBody2D(const Body2DDesc& desc) = 0;
  virtual int CreateRigidBody2D(const Body2DDesc& desc) = 0;
  virtual int CreateCharacterBody2D(const Body2DDesc& desc) = 0;
  virtual int CreateArea2D(const Body2DDesc& desc) = 0;

  virtual void SetLinearVelocity(int body_id, const Vec2& v) = 0;
  [[nodiscard]] virtual Vec2 GetLinearVelocity(int body_id) const = 0;
  virtual void SetAngularVelocity(int body_id, float w) = 0;
  [[nodiscard]] virtual float GetAngularVelocity(int body_id) const = 0;

  // CharacterBody2D Godot-like API
  virtual Status MoveAndSlide(int character_id, const Vec2& velocity, float dt,
                              const CharacterMove2DParams& params = {}) = 0;
  [[nodiscard]] virtual bool IsOnFloor(int character_id) const = 0;
  [[nodiscard]] virtual bool IsOnWall(int character_id) const = 0;
  [[nodiscard]] virtual bool IsOnCeiling(int character_id) const = 0;
  [[nodiscard]] virtual Vec2 GetFloorNormal(int character_id) const = 0;

  [[nodiscard]] virtual Vec2 body_position(int body_id) const = 0;
  virtual void set_body_position(int body_id, const Vec2& p) = 0;
  [[nodiscard]] virtual float body_rotation(int body_id) const = 0;
  [[nodiscard]] virtual BodyType2D body_type(int body_id) const = 0;
  [[nodiscard]] virtual int body_count() const = 0;

  virtual void SetCollisionLayer(int body_id, std::uint32_t layer) = 0;
  virtual void SetCollisionMask(int body_id, std::uint32_t mask) = 0;
  [[nodiscard]] virtual std::uint32_t GetCollisionLayer(int body_id) const = 0;
  [[nodiscard]] virtual std::uint32_t GetCollisionMask(int body_id) const = 0;

  [[nodiscard]] virtual RayHit2D Raycast2D(const Vec2& origin, const Vec2& dir, float max_dist,
                                           std::uint32_t mask = 0xFFFFFFFFu) const = 0;
  [[nodiscard]] virtual RayHit2D ShapeCast2D(const Shape2DDesc& shape, const Vec2& origin,
                                            const Vec2& motion, std::uint32_t mask = 0xFFFFFFFFu) const = 0;

  virtual int CreateJoint2D(const Joint2DDesc& desc) = 0;
  virtual bool DestroyJoint2D(int joint_id) = 0;
  [[nodiscard]] virtual int joint_count() const = 0;

  virtual void Step(float dt) = 0;
  [[nodiscard]] virtual std::vector<AreaEvent2D> ConsumeAreaEvents() = 0;
  [[nodiscard]] virtual std::vector<ContactPair2D> ConsumeContacts() = 0;

  [[nodiscard]] virtual const char* backend_name() const = 0;
};

std::unique_ptr<IPhysicsWorld2D> CreateBuiltinPhysicsWorld2D();
std::unique_ptr<IPhysicsWorld2D> CreateBox2DPhysicsWorld2D();  // nullptr if ENGINE_WITH_BOX2D=0
std::unique_ptr<IPhysicsWorld2D> CreateDefaultPhysicsWorld2D();

}  // namespace engine::physics
