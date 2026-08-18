#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cmath>
#include <cstdint>
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

struct ContactPair {
  int a = -1;
  int b = -1;
};

struct RigidBodyDesc {
  Vec3 position{};
  Vec3 half_extents{0.5f, 0.5f, 0.5f};
  float mass = 1.f;
  bool is_trigger = false;
};

// Capsule standing on Y: total height ≈ 2*(half_height + radius); center at position.
struct CapsuleDesc {
  Vec3 position{};
  float radius = 0.35f;
  float half_height = 0.5f;  // cylinder half-height between hemispheres
  float mass = 0.f;          // 0 → kinematic character body
};

// Thin SoftBody / Cloth (ADR 0029 / C22). Unsupported backends return -1 / false.
struct SoftBodyDesc {
  Vec3 position{};
  int grid = 6;       // cube grid resolution (vertices per edge)
  float cell = 0.2f;  // spacing between vertices
  float mass = 1.f;   // total mass (>0); distributed across free vertices
};

class IPhysicsWorld {
 public:
  virtual ~IPhysicsWorld() = default;
  virtual int CreateBox(const RigidBodyDesc& desc) = 0;
  // Capsule character/body; backends may approximate with a tall box if needed.
  virtual int CreateCapsule(const CapsuleDesc& desc) = 0;
  virtual void Step(float dt) = 0;
  virtual RayHit Raycast(const Vec3& origin, const Vec3& dir, float max_dist) const = 0;
  // Horizontal move + ground snap / simple collision (not Y-clamp only).
  virtual Status MoveCharacter(int body_id, const Vec3& displacement) = 0;
  [[nodiscard]] virtual Vec3 body_position(int body_id) const = 0;
  [[nodiscard]] virtual Vec3 body_half_extents(int body_id) const = 0;
  [[nodiscard]] virtual int body_count() const = 0;
  [[nodiscard]] virtual const char* backend_name() const = 0;

  // SoftBody: default stubs SKIP (builtin / non-Jolt). Jolt adapter overrides.
  virtual int CreateSoftBody(const SoftBodyDesc& /*desc*/) { return -1; }
  virtual bool SoftBodyGetVertices(int /*id*/, std::vector<Vec3>& /*out_world*/) { return false; }
  [[nodiscard]] virtual int SoftBodyGetIndexCount(int /*id*/) const { return 0; }
  virtual bool SoftBodyGetIndices(int /*id*/, std::vector<std::uint32_t>& /*out*/) { return false; }

  // Mega-W8 buoyancy: apply linear impulse (Δv = impulse / mass). Default false.
  virtual bool ApplyImpulse(int /*body_id*/, const Vec3& /*impulse*/) { return false; }

  // Host API v0: AABB overlap query. Default walks body_position / half_extents.
  virtual std::vector<int> OverlapAabb(const Vec3& center, const Vec3& half) const {
    std::vector<int> out;
    const int n = body_count();
    for (int i = 0; i < n; ++i) {
      const Vec3 p = body_position(i);
      const Vec3 h = body_half_extents(i);
      if (std::fabs(p.x - center.x) <= h.x + half.x && std::fabs(p.y - center.y) <= h.y + half.y &&
          std::fabs(p.z - center.z) <= h.z + half.z) {
        out.push_back(i);
      }
    }
    return out;
  }

  // Host API v0: contacts since last consume. Default synthesizes AABB pairs.
  virtual std::vector<ContactPair> ConsumeContacts() {
    std::vector<ContactPair> out;
    const int n = body_count();
    for (int i = 0; i < n; ++i) {
      const Vec3 pa = body_position(i);
      const Vec3 ha = body_half_extents(i);
      for (int j = i + 1; j < n; ++j) {
        const Vec3 pb = body_position(j);
        const Vec3 hb = body_half_extents(j);
        if (std::fabs(pa.x - pb.x) <= ha.x + hb.x && std::fabs(pa.y - pb.y) <= ha.y + hb.y &&
            std::fabs(pa.z - pb.z) <= ha.z + hb.z) {
          out.push_back(ContactPair{i, j});
        }
      }
    }
    return out;
  }
};

std::unique_ptr<IPhysicsWorld> CreateBuiltinPhysicsWorld();

// Real Jolt adapter when ENGINE_WITH_JOLT=1; otherwise nullptr.
std::unique_ptr<IPhysicsWorld> CreateJoltPhysicsWorld();

// Prefers Jolt when available; otherwise builtin.
std::unique_ptr<IPhysicsWorld> CreateDefaultPhysicsWorld();

}  // namespace engine::physics
