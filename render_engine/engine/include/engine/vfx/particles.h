#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <vector>

namespace engine::vfx {

struct Particle {
  Vec3 position{};
  Vec3 velocity{};
  ColorRgba color{1, 0.7f, 0.3f, 1};
  float life = 1.f;
  float size = 4.f;
};

// M7: minimal CPU particle emitter (screen/world helpers live in caller).
class ParticleEmitter {
 public:
  void Configure(const Vec3& origin, float rate, float lifetime);
  void set_origin(const Vec3& o) { origin_ = o; }
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] const std::vector<Particle>& particles() const { return particles_; }

  void EmitBurst(int count);
  void Step(float dt);

 private:
  bool enabled_ = true;
  Vec3 origin_{};
  float rate_ = 20.f;
  float lifetime_ = 1.2f;
  float emit_accum_ = 0.f;
  std::uint32_t rng_ = 1u;
  std::vector<Particle> particles_;

  float NextRand();
};

}  // namespace engine::vfx
