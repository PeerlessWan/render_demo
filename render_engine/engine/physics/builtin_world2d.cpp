#include "engine/physics/i_physics_world2d.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace engine::physics {
namespace {

struct Body2D {
  int id = -1;
  BodyType2D type = BodyType2D::Static;
  Vec2 position{};
  float rotation = 0.f;
  Shape2DDesc shape{};
  float mass = 1.f;
  Vec2 linear_vel{};
  float angular_vel = 0.f;
  float linear_damping = 0.f;
  float angular_damping = 0.f;
  std::uint32_t layer = 1u;
  std::uint32_t mask = 0xFFFFFFFFu;
  bool sleeping = false;
  // Character state
  bool on_floor = false;
  bool on_wall = false;
  bool on_ceiling = false;
  Vec2 floor_normal{0.f, -1.f};
};

struct Joint2D {
  int id = -1;
  Joint2DDesc desc{};
  bool alive = true;
};

struct Aabb {
  float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
};

Aabb ShapeAabb(const Vec2& p, const Shape2DDesc& s) {
  Aabb a;
  if (s.type == ShapeType2D::Circle) {
    a.min_x = p.x - s.radius;
    a.max_x = p.x + s.radius;
    a.min_y = p.y - s.radius;
    a.max_y = p.y + s.radius;
  } else if (s.type == ShapeType2D::Capsule) {
    const float r = s.radius > 0.f ? s.radius : s.half_extents.x;
    const float hh = s.half_extents.y;
    a.min_x = p.x - r;
    a.max_x = p.x + r;
    a.min_y = p.y - hh - r;
    a.max_y = p.y + hh + r;
  } else {
    a.min_x = p.x - s.half_extents.x;
    a.max_x = p.x + s.half_extents.x;
    a.min_y = p.y - s.half_extents.y;
    a.max_y = p.y + s.half_extents.y;
  }
  return a;
}

bool AabbOverlap(const Aabb& a, const Aabb& b) {
  return a.min_x <= b.max_x && a.max_x >= b.min_x && a.min_y <= b.max_y && a.max_y >= b.min_y;
}

bool LayersMatch(std::uint32_t a_layer, std::uint32_t a_mask, std::uint32_t b_layer,
                 std::uint32_t b_mask) {
  return (a_mask & b_layer) != 0 && (b_mask & a_layer) != 0;
}

}  // namespace

class BuiltinPhysicsWorld2D final : public IPhysicsWorld2D {
 public:
  int CreateStaticBody2D(const Body2DDesc& desc) override {
    return Alloc(BodyType2D::Static, desc);
  }
  int CreateRigidBody2D(const Body2DDesc& desc) override { return Alloc(BodyType2D::Rigid, desc); }
  int CreateCharacterBody2D(const Body2DDesc& desc) override {
    return Alloc(BodyType2D::Character, desc);
  }
  int CreateArea2D(const Body2DDesc& desc) override {
    Body2DDesc d = desc;
    d.is_trigger = true;
    d.mass = 0.f;
    return Alloc(BodyType2D::Area, d);
  }

  void SetLinearVelocity(int body_id, const Vec2& v) override {
    if (auto* b = Find(body_id)) {
      b->linear_vel = v;
      b->sleeping = false;
    }
  }
  Vec2 GetLinearVelocity(int body_id) const override {
    if (const auto* b = Find(body_id)) {
      return b->linear_vel;
    }
    return {};
  }
  void SetAngularVelocity(int body_id, float w) override {
    if (auto* b = Find(body_id)) {
      b->angular_vel = w;
    }
  }
  float GetAngularVelocity(int body_id) const override {
    if (const auto* b = Find(body_id)) {
      return b->angular_vel;
    }
    return 0.f;
  }

  Status MoveAndSlide(int character_id, const Vec2& velocity, float dt,
                      const CharacterMove2DParams& params) override {
    auto* ch = Find(character_id);
    if (!ch || ch->type != BodyType2D::Character) {
      return Status::Fail("invalid character");
    }
    ch->linear_vel = velocity;
    ch->on_floor = false;
    ch->on_wall = false;
    ch->on_ceiling = false;
    ch->floor_normal = {0.f, -1.f};

    Vec2 rem = {velocity.x * dt, velocity.y * dt};
    for (int iter = 0; iter < 4; ++iter) {
      if (std::fabs(rem.x) + std::fabs(rem.y) < 1e-6f) {
        break;
      }
      const Vec2 try_pos = {ch->position.x + rem.x, ch->position.y + rem.y};
      int hit = -1;
      Vec2 n{};
      if (!ResolvePenetration(*ch, try_pos, &hit, &n)) {
        ch->position = try_pos;
        rem = {};
        break;
      }
      // Slide along normal
      if (n.y < -0.5f) {
        ch->on_floor = true;
        ch->floor_normal = n;
      } else if (n.y > 0.5f) {
        ch->on_ceiling = true;
      } else {
        ch->on_wall = true;
      }
      const float nd = rem.x * n.x + rem.y * n.y;
      if (nd < 0.f) {
        rem.x -= n.x * nd;
        rem.y -= n.y * nd;
      } else {
        rem.x *= 0.5f;
        rem.y *= 0.5f;
      }
      // Snap down for floors
      if (params.snap_length > 0.f && velocity.y >= 0.f) {
        const Vec2 snap = {ch->position.x, ch->position.y + params.snap_length};
        int sh = -1;
        Vec2 sn{};
        if (ResolvePenetration(*ch, snap, &sh, &sn) && sn.y < -0.5f) {
          ch->on_floor = true;
          ch->floor_normal = sn;
        }
      }
      (void)params;
    }
    return Status::Ok();
  }

  bool IsOnFloor(int id) const override {
    const auto* b = Find(id);
    return b && b->on_floor;
  }
  bool IsOnWall(int id) const override {
    const auto* b = Find(id);
    return b && b->on_wall;
  }
  bool IsOnCeiling(int id) const override {
    const auto* b = Find(id);
    return b && b->on_ceiling;
  }
  Vec2 GetFloorNormal(int id) const override {
    const auto* b = Find(id);
    return b ? b->floor_normal : Vec2{0.f, -1.f};
  }

  Vec2 body_position(int id) const override {
    const auto* b = Find(id);
    return b ? b->position : Vec2{};
  }
  void set_body_position(int id, const Vec2& p) override {
    if (auto* b = Find(id)) {
      b->position = p;
    }
  }
  float body_rotation(int id) const override {
    const auto* b = Find(id);
    return b ? b->rotation : 0.f;
  }
  BodyType2D body_type(int id) const override {
    const auto* b = Find(id);
    return b ? b->type : BodyType2D::Static;
  }
  int body_count() const override { return static_cast<int>(bodies_.size()); }

  void SetCollisionLayer(int id, std::uint32_t layer) override {
    if (auto* b = Find(id)) {
      b->layer = layer;
    }
  }
  void SetCollisionMask(int id, std::uint32_t mask) override {
    if (auto* b = Find(id)) {
      b->mask = mask;
    }
  }
  std::uint32_t GetCollisionLayer(int id) const override {
    const auto* b = Find(id);
    return b ? b->layer : 0u;
  }
  std::uint32_t GetCollisionMask(int id) const override {
    const auto* b = Find(id);
    return b ? b->mask : 0u;
  }

  RayHit2D Raycast2D(const Vec2& origin, const Vec2& dir, float max_dist,
                     std::uint32_t mask) const override {
    RayHit2D best;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 1e-8f || max_dist <= 0.f) {
      return best;
    }
    const Vec2 nd = {dir.x / len, dir.y / len};
    const int steps = 64;
    for (int i = 1; i <= steps; ++i) {
      const float t = max_dist * (static_cast<float>(i) / static_cast<float>(steps));
      const Vec2 p = {origin.x + nd.x * t, origin.y + nd.y * t};
      for (const auto& b : bodies_) {
        if (b.type == BodyType2D::Area) {
          continue;
        }
        if ((mask & b.layer) == 0) {
          continue;
        }
        const Aabb a = ShapeAabb(b.position, b.shape);
        if (p.x >= a.min_x && p.x <= a.max_x && p.y >= a.min_y && p.y <= a.max_y) {
          best.hit = true;
          best.point = p;
          best.distance = t;
          best.body_id = b.id;
          // Approximate normal from center
          const float dx = p.x - b.position.x;
          const float dy = p.y - b.position.y;
          if (std::fabs(dx) > std::fabs(dy)) {
            best.normal = {dx > 0 ? 1.f : -1.f, 0.f};
          } else {
            best.normal = {0.f, dy > 0 ? 1.f : -1.f};
          }
          return best;
        }
      }
    }
    return best;
  }

  RayHit2D ShapeCast2D(const Shape2DDesc& shape, const Vec2& origin, const Vec2& motion,
                       std::uint32_t mask) const override {
    RayHit2D best;
    const int steps = 32;
    for (int i = 1; i <= steps; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      const Vec2 p = {origin.x + motion.x * t, origin.y + motion.y * t};
      const Aabb moving = ShapeAabb(p, shape);
      for (const auto& b : bodies_) {
        if (b.type == BodyType2D::Area) {
          continue;
        }
        if ((mask & b.layer) == 0) {
          continue;
        }
        if (AabbOverlap(moving, ShapeAabb(b.position, b.shape))) {
          best.hit = true;
          best.point = p;
          best.distance = t * std::sqrt(motion.x * motion.x + motion.y * motion.y);
          best.body_id = b.id;
          best.normal = {0.f, -1.f};
          return best;
        }
      }
    }
    return best;
  }

  int CreateJoint2D(const Joint2DDesc& desc) override {
    if (!Find(desc.body_a) || !Find(desc.body_b)) {
      return -1;
    }
    Joint2D j;
    j.id = next_joint_++;
    j.desc = desc;
    joints_.push_back(j);
    return j.id;
  }
  bool DestroyJoint2D(int joint_id) override {
    for (auto& j : joints_) {
      if (j.id == joint_id && j.alive) {
        j.alive = false;
        return true;
      }
    }
    return false;
  }
  int joint_count() const override {
    int n = 0;
    for (const auto& j : joints_) {
      if (j.alive) {
        ++n;
      }
    }
    return n;
  }

  void Step(float dt) override {
    if (dt <= 0.f) {
      return;
    }
    // Integrate rigid bodies
    for (auto& b : bodies_) {
      if (b.type != BodyType2D::Rigid || b.sleeping) {
        continue;
      }
      b.linear_vel.x *= std::max(0.f, 1.f - b.linear_damping * dt);
      b.linear_vel.y *= std::max(0.f, 1.f - b.linear_damping * dt);
      b.angular_vel *= std::max(0.f, 1.f - b.angular_damping * dt);
      b.position.x += b.linear_vel.x * dt;
      b.position.y += b.linear_vel.y * dt;
      b.rotation += b.angular_vel * dt;
      // Resolve vs static
      for (const auto& o : bodies_) {
        if (o.id == b.id || o.type == BodyType2D::Area || o.type == BodyType2D::Character) {
          continue;
        }
        if (!LayersMatch(b.layer, b.mask, o.layer, o.mask)) {
          continue;
        }
        const Aabb ba = ShapeAabb(b.position, b.shape);
        const Aabb oa = ShapeAabb(o.position, o.shape);
        if (!AabbOverlap(ba, oa)) {
          continue;
        }
        const float ox = std::min(ba.max_x, oa.max_x) - std::max(ba.min_x, oa.min_x);
        const float oy = std::min(ba.max_y, oa.max_y) - std::max(ba.min_y, oa.min_y);
        if (ox < oy) {
          const float dir = (b.position.x < o.position.x) ? -1.f : 1.f;
          b.position.x += dir * ox;
          b.linear_vel.x = 0.f;
        } else {
          const float dir = (b.position.y < o.position.y) ? -1.f : 1.f;
          b.position.y += dir * oy;
          b.linear_vel.y = 0.f;
        }
        contacts_.push_back({b.id, o.id});
      }
    }
    // Pin joints: pull bodies toward shared distance
    for (const auto& j : joints_) {
      if (!j.alive) {
        continue;
      }
      auto* a = Find(j.desc.body_a);
      auto* b = Find(j.desc.body_b);
      if (!a || !b) {
        continue;
      }
      const Vec2 wa = {a->position.x + j.desc.anchor_a.x, a->position.y + j.desc.anchor_a.y};
      const Vec2 wb = {b->position.x + j.desc.anchor_b.x, b->position.y + j.desc.anchor_b.y};
      const float dx = wb.x - wa.x;
      const float dy = wb.y - wa.y;
      const float dist = std::sqrt(dx * dx + dy * dy);
      if (dist < 1e-6f) {
        continue;
      }
      const float corr = 0.5f * dist;
      const float nx = dx / dist;
      const float ny = dy / dist;
      if (a->type == BodyType2D::Rigid) {
        a->position.x += nx * corr;
        a->position.y += ny * corr;
      }
      if (b->type == BodyType2D::Rigid) {
        b->position.x -= nx * corr;
        b->position.y -= ny * corr;
      }
      if (j.desc.type == JointType2D::Hinge) {
        // Keep relative rotation loosely damped (teaching-grade hinge)
        const float mid = 0.5f * (a->angular_vel + b->angular_vel);
        if (a->type == BodyType2D::Rigid) {
          a->angular_vel = mid;
        }
        if (b->type == BodyType2D::Rigid) {
          b->angular_vel = mid;
        }
      }
    }
    UpdateAreas();
  }

  std::vector<AreaEvent2D> ConsumeAreaEvents() override {
    auto out = std::move(area_events_);
    area_events_.clear();
    return out;
  }
  std::vector<ContactPair2D> ConsumeContacts() override {
    auto out = std::move(contacts_);
    contacts_.clear();
    return out;
  }
  const char* backend_name() const override { return "builtin2d"; }

 private:
  int Alloc(BodyType2D type, const Body2DDesc& desc) {
    Body2D b;
    b.id = next_id_++;
    b.type = type;
    b.position = desc.position;
    b.shape = desc.shape;
    b.mass = desc.mass;
    b.linear_damping = desc.linear_damping;
    b.angular_damping = desc.angular_damping;
    b.layer = desc.collision_layer;
    b.mask = desc.collision_mask;
    bodies_.push_back(b);
    return b.id;
  }

  Body2D* Find(int id) {
    for (auto& b : bodies_) {
      if (b.id == id) {
        return &b;
      }
    }
    return nullptr;
  }
  const Body2D* Find(int id) const {
    for (const auto& b : bodies_) {
      if (b.id == id) {
        return &b;
      }
    }
    return nullptr;
  }

  bool ResolvePenetration(const Body2D& ch, const Vec2& try_pos, int* hit_id, Vec2* normal) const {
    const Aabb ca = ShapeAabb(try_pos, ch.shape);
    for (const auto& o : bodies_) {
      if (o.id == ch.id || o.type == BodyType2D::Area || o.type == BodyType2D::Character) {
        continue;
      }
      if (!LayersMatch(ch.layer, ch.mask, o.layer, o.mask)) {
        continue;
      }
      const Aabb oa = ShapeAabb(o.position, o.shape);
      if (!AabbOverlap(ca, oa)) {
        continue;
      }
      if (hit_id) {
        *hit_id = o.id;
      }
      if (normal) {
        const float ox = std::min(ca.max_x, oa.max_x) - std::max(ca.min_x, oa.min_x);
        const float oy = std::min(ca.max_y, oa.max_y) - std::max(ca.min_y, oa.min_y);
        if (ox < oy) {
          *normal = {try_pos.x < o.position.x ? -1.f : 1.f, 0.f};
        } else {
          *normal = {0.f, try_pos.y < o.position.y ? -1.f : 1.f};
        }
      }
      return true;
    }
    return false;
  }

  void UpdateAreas() {
    std::unordered_set<std::uint64_t> now;
    for (const auto& area : bodies_) {
      if (area.type != BodyType2D::Area) {
        continue;
      }
      const Aabb aa = ShapeAabb(area.position, area.shape);
      for (const auto& o : bodies_) {
        if (o.id == area.id || o.type == BodyType2D::Area) {
          continue;
        }
        if (!LayersMatch(area.layer, area.mask, o.layer, o.mask)) {
          continue;
        }
        if (!AabbOverlap(aa, ShapeAabb(o.position, o.shape))) {
          continue;
        }
        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(area.id)) << 32) |
            static_cast<std::uint32_t>(o.id);
        now.insert(key);
        if (!area_pairs_.count(key)) {
          area_events_.push_back({area.id, o.id, true});
        }
      }
    }
    for (const auto& key : area_pairs_) {
      if (!now.count(key)) {
        const int area_id = static_cast<int>(key >> 32);
        const int other = static_cast<int>(key & 0xffffffffu);
        area_events_.push_back({area_id, other, false});
      }
    }
    area_pairs_ = std::move(now);
  }

  std::vector<Body2D> bodies_;
  std::vector<Joint2D> joints_;
  std::vector<AreaEvent2D> area_events_;
  std::vector<ContactPair2D> contacts_;
  std::unordered_set<std::uint64_t> area_pairs_;
  int next_id_ = 1;
  int next_joint_ = 1;
};

std::unique_ptr<IPhysicsWorld2D> CreateBuiltinPhysicsWorld2D() {
  return std::make_unique<BuiltinPhysicsWorld2D>();
}

std::unique_ptr<IPhysicsWorld2D> CreateBox2DPhysicsWorld2D() {
  // Real Box2D adapter lands behind ENGINE_WITH_BOX2D; until then prefer honest nullptr
  // so CreateDefaultPhysicsWorld2D → builtin2d (ADR 0049).
  return nullptr;
}

std::unique_ptr<IPhysicsWorld2D> CreateDefaultPhysicsWorld2D() {
  if (auto b = CreateBox2DPhysicsWorld2D()) {
    return b;
  }
  return CreateBuiltinPhysicsWorld2D();
}

}  // namespace engine::physics
