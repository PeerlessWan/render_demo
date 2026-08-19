#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/vfx/particles.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::vfx {

// W14 / ADR 0039: GPU particle path. When compute is unavailable, Step falls back to
// CPU ParticleEmitter semantics and reports path via last_path().
class GpuParticleSystem {
 public:
  void Configure(const Vec3& origin, float rate, float lifetime, std::uint32_t max_particles = 256);
  void set_origin(const Vec3& o) { origin_ = o; }
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] const std::vector<Particle>& particles() const { return particles_; }
  [[nodiscard]] const char* last_path() const { return last_path_.c_str(); }

  // Prefer GPU CS update; on failure use CPU integrate (honest path string).
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

  float NextRand();
  void EmitCpu(int count);
  void IntegrateCpu(float dt);
};

}  // namespace engine::vfx
