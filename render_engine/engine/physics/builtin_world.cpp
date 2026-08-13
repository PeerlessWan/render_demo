#include "engine/physics/i_physics_world.h"

#include <algorithm>
#include <cmath>

namespace engine::physics {
namespace {

struct Body {
  Vec3 position{};
  Vec3 half_extents{0.5f, 0.5f, 0.5f};
  Vec3 velocity{};
  float mass = 1.f;
  bool is_trigger = false;
  bool is_character = false;
};

class BuiltinWorld final : public IPhysicsWorld {
 public:
  int CreateBox(const RigidBodyDesc& desc) override {
    Body b;
    b.position = desc.position;
    b.half_extents = desc.half_extents;
    b.mass = desc.mass;
    b.is_trigger = desc.is_trigger;
    bodies_.push_back(b);
    return static_cast<int>(bodies_.size() - 1);
  }

  void Step(float dt) override {
    constexpr float kGravity = -9.81f;
    for (auto& b : bodies_) {
      if (b.mass <= 0.f || b.is_character) {
        continue;
      }
      b.velocity.y += kGravity * dt;
      b.position = b.position + b.velocity * dt;
      const float floor_y = b.half_extents.y;
      if (b.position.y < floor_y) {
        b.position.y = floor_y;
        b.velocity.y = 0.f;
      }
    }
  }

  RayHit Raycast(const Vec3& origin, const Vec3& dir_in, float max_dist) const override {
    const Vec3 dir = Normalize(dir_in);
    RayHit best;
    best.distance = max_dist;
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
      const auto& b = bodies_[static_cast<std::size_t>(i)];
      // Slab test against AABB.
      float tmin = 0.f;
      float tmax = max_dist;
      bool miss = false;
      const float* o = &origin.x;
      const float* d = &dir.x;
      const float mn[3] = {b.position.x - b.half_extents.x, b.position.y - b.half_extents.y,
                           b.position.z - b.half_extents.z};
      const float mx[3] = {b.position.x + b.half_extents.x, b.position.y + b.half_extents.y,
                           b.position.z + b.half_extents.z};
      for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-8f) {
          if (o[a] < mn[a] || o[a] > mx[a]) {
            miss = true;
            break;
          }
          continue;
        }
        float t1 = (mn[a] - o[a]) / d[a];
        float t2 = (mx[a] - o[a]) / d[a];
        if (t1 > t2) {
          std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
          miss = true;
          break;
        }
      }
      if (!miss && tmin >= 0.f && tmin < best.distance) {
        best.hit = true;
        best.distance = tmin;
        best.point = origin + dir * tmin;
        best.body_id = i;
        best.normal = Vec3{0, 1, 0};
      }
    }
    return best;
  }

  Status MoveCharacter(int body_id, const Vec3& displacement) override {
    if (body_id < 0 || body_id >= static_cast<int>(bodies_.size())) {
      return Status::Fail(ErrorCode::NotFound, "body not found");
    }
    auto& b = bodies_[static_cast<std::size_t>(body_id)];
    b.is_character = true;
    b.position = b.position + displacement;
    if (b.position.y < b.half_extents.y) {
      b.position.y = b.half_extents.y;
    }
    return Status::Ok();
  }

  Vec3 body_position(int body_id) const override {
    if (body_id < 0 || body_id >= static_cast<int>(bodies_.size())) {
      return {};
    }
    return bodies_[static_cast<std::size_t>(body_id)].position;
  }

  Vec3 body_half_extents(int body_id) const override {
    if (body_id < 0 || body_id >= static_cast<int>(bodies_.size())) {
      return {};
    }
    return bodies_[static_cast<std::size_t>(body_id)].half_extents;
  }

  int body_count() const override { return static_cast<int>(bodies_.size()); }

  const char* backend_name() const override { return "builtin"; }

 private:
  std::vector<Body> bodies_;
};

}  // namespace

std::unique_ptr<IPhysicsWorld> CreateBuiltinPhysicsWorld() {
  return std::make_unique<BuiltinWorld>();
}

}  // namespace engine::physics
