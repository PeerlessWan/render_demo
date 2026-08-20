#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/vfx/particles.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::vfx {

// W16 / ADR 0040: GPU particle path. Feature gpu_particles → D3D12 CS integrate +
// IndirectDrawArgs; on failure honest cpu-fallback (never fake gpu-contract names).
class GpuParticleSystem {
 public:
  void Configure(const Vec3& origin, float rate, float lifetime, std::uint32_t max_particles = 256);
  void set_origin(const Vec3& o) { origin_ = o; }
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] const std::vector<Particle>& particles() const { return particles_; }
  [[nodiscard]] const char* last_path() const { return last_path_.c_str(); }
  [[nodiscard]] const gpu_driven::IndirectDrawArgs& last_indirect() const { return last_indirect_; }

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

  float NextRand();
  void EmitCpu(int count);
  void IntegrateCpu(float dt);
  void CullDead();
  void FillIndirect();
};

}  // namespace engine::vfx
