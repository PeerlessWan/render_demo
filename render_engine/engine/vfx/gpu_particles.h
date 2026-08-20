#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/vfx/particles.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::vfx {

struct ParticleCollisionPlane {
  Vec3 point{0.f, 0.f, 0.f};
  Vec3 normal{0.f, 1.f, 0.f};
  float bounce = 0.35f;
  bool enabled = false;
};

struct ParticleKillBox {
  Vec3 min_p{-1e9f, -1e9f, -1e9f};
  Vec3 max_p{1e9f, 1e9f, 1e9f};
  bool enabled = false;
};

struct ParticleSubEmit {
  bool enabled = false;
  float rate = 8.f;
  float lifetime = 0.35f;
  float size_scale = 0.45f;
};

// W16 / ADR 0040 + W21 ADR 0044: GPU particle path + collision / sub-emit.
class GpuParticleSystem {
 public:
  void Configure(const Vec3& origin, float rate, float lifetime, std::uint32_t max_particles = 256);
  void set_origin(const Vec3& o) { origin_ = o; }
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] const std::vector<Particle>& particles() const { return particles_; }
  [[nodiscard]] const char* last_path() const { return last_path_.c_str(); }
  [[nodiscard]] const gpu_driven::IndirectDrawArgs& last_indirect() const { return last_indirect_; }

  void set_collision_plane(const ParticleCollisionPlane& p) { collision_ = p; }
  void set_kill_box(const ParticleKillBox& box) { kill_box_ = box; }
  void set_sub_emit(const ParticleSubEmit& s) { sub_emit_ = s; }
  [[nodiscard]] const ParticleCollisionPlane& collision_plane() const { return collision_; }
  [[nodiscard]] const ParticleKillBox& kill_box() const { return kill_box_; }
  [[nodiscard]] const ParticleSubEmit& sub_emit() const { return sub_emit_; }

  Status Step(float dt);

 private:
  bool enabled_ = true;
  Vec3 origin_{};
  float rate_ = 28.f;
  float lifetime_ = 1.1f;
  float emit_accum_ = 0.f;
  std::uint32_t max_particles_ = 256;
  std::uint32_t rng_ = 7u;
  std::vector<Particle> particles_;
  std::string last_path_{"idle"};
  gpu_driven::IndirectDrawArgs last_indirect_{};
  ParticleCollisionPlane collision_{};
  ParticleKillBox kill_box_{};
  ParticleSubEmit sub_emit_{};
  float sub_accum_ = 0.f;

  float NextRand();
  void EmitCpu(int count);
  void EmitSubCpu(const Particle& parent, int count);
  void IntegrateCpu(float dt);
  void ApplyCollisionAndKill();
  void CullDead();
  void FillIndirect();
};

}  // namespace engine::vfx
