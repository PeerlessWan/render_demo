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

  int CreateCapsule(const CapsuleDesc& desc) override {
    // Builtin approximates capsule as a tall AABB.
    RigidBodyDesc box;
    box.position = desc.position;
    box.half_extents = {desc.radius, desc.half_height + desc.radius, desc.radius};
    box.mass = desc.mass;
    const int id = CreateBox(box);
    if (id >= 0) {
      bodies_[static_cast<std::size_t>(id)].is_character = true;
    }
    return id;
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

    // Horizontal move with AABB separation against other bodies.
    Vec3 next = b.position;
    next.x += displacement.x;
    next.z += displacement.z;
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
      if (i == body_id) {
        continue;
      }
      const auto& o = bodies_[static_cast<std::size_t>(i)];
      if (o.is_trigger) {
        continue;
      }
      const float dx = std::fabs(next.x - o.position.x);
      const float dz = std::fabs(next.z - o.position.z);
      const float sx = b.half_extents.x + o.half_extents.x;
      const float sz = b.half_extents.z + o.half_extents.z;
      const float dy = std::fabs(b.position.y - o.position.y);
      const float sy = b.half_extents.y + o.half_extents.y;
      if (dx < sx && dz < sz && dy < sy) {
        // Push out on the shallow horizontal axis.
        if (sx - dx < sz - dz) {
          next.x = o.position.x + (next.x >= o.position.x ? sx : -sx);
        } else {
          next.z = o.position.z + (next.z >= o.position.z ? sz : -sz);
        }
      }
    }
    b.position.x = next.x;
    b.position.z = next.z;
    b.position.y += displacement.y;

    // Ground snap: downward AABB probe excluding self.
    const float feet_y = b.position.y - b.half_extents.y;
    const Vec3 origin{b.position.x, b.position.y + 0.05f, b.position.z};
    const Vec3 dir{0.f, -1.f, 0.f};
    const float max_dist = b.half_extents.y * 2.f + 0.5f;
    RayHit ground;
    ground.distance = max_dist;
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
      if (i == body_id) {
        continue;
      }
      const auto& o = bodies_[static_cast<std::size_t>(i)];
      if (o.is_trigger) {
        continue;
      }
      const float* org = &origin.x;
      const float d[3] = {0.f, -1.f, 0.f};
      const float mn[3] = {o.position.x - o.half_extents.x, o.position.y - o.half_extents.y,
                           o.position.z - o.half_extents.z};
      const float mx[3] = {o.position.x + o.half_extents.x, o.position.y + o.half_extents.y,
                           o.position.z + o.half_extents.z};
      float tmin = 0.f;
      float tmax = max_dist;
      bool miss = false;
      for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-8f) {
          if (org[a] < mn[a] || org[a] > mx[a]) {
            miss = true;
            break;
          }
          continue;
        }
        float t1 = (mn[a] - org[a]) / d[a];
        float t2 = (mx[a] - org[a]) / d[a];
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
      if (!miss && tmin >= 0.f && tmin < ground.distance) {
        ground.hit = true;
        ground.distance = tmin;
        ground.point = origin + dir * tmin;
        ground.body_id = i;
      }
    }
    if (ground.hit) {
      const float ground_top = ground.point.y;
      if (feet_y <= ground_top + 0.35f) {
        b.position.y = ground_top + b.half_extents.y;
        b.velocity.y = 0.f;
      }
    } else if (b.position.y < b.half_extents.y) {
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

  // SoftBody unsupported on builtin (ADR 0029 / C22 SKIP).
  int CreateSoftBody(const SoftBodyDesc& /*desc*/) override { return -1; }

  bool SoftBodyGetVertices(int /*id*/, std::vector<Vec3>& /*out_world*/) override { return false; }

  int SoftBodyGetIndexCount(int /*id*/) const override { return 0; }

  bool SoftBodyGetIndices(int /*id*/, std::vector<std::uint32_t>& /*out*/) override { return false; }

  bool ApplyImpulse(int body_id, const Vec3& impulse) override {
    if (body_id < 0 || body_id >= static_cast<int>(bodies_.size())) {
      return false;
    }
    auto& b = bodies_[static_cast<std::size_t>(body_id)];
    if (b.mass <= 0.f || b.is_character) {
      return false;
    }
    b.velocity = b.velocity + impulse * (1.f / b.mass);
    return true;
  }

 private:
  std::vector<Body> bodies_;
};

}  // namespace

std::unique_ptr<IPhysicsWorld> CreateBuiltinPhysicsWorld() {
  return std::make_unique<BuiltinWorld>();
}

}  // namespace engine::physics
