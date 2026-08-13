#include "engine/vfx/particles.h"

#include <algorithm>
#include <cmath>

namespace engine::vfx {

void ParticleEmitter::Configure(const Vec3& origin, float rate, float lifetime) {
  origin_ = origin;
  rate_ = std::max(0.f, rate);
  lifetime_ = std::max(0.05f, lifetime);
}

float ParticleEmitter::NextRand() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<float>(rng_ & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void ParticleEmitter::EmitBurst(int count) {
  if (!enabled_ || count <= 0) {
    return;
  }
  for (int i = 0; i < count; ++i) {
    Particle p;
    p.position = origin_;
    const float yaw = NextRand() * 6.2831853f;
    const float speed = 1.5f + NextRand() * 3.5f;
    p.velocity = {std::cos(yaw) * speed, 2.f + NextRand() * 4.f, std::sin(yaw) * speed};
    p.life = lifetime_ * (0.6f + NextRand() * 0.4f);
    p.size = 3.f + NextRand() * 5.f;
    p.color = {1.f, 0.55f + NextRand() * 0.35f, 0.15f + NextRand() * 0.25f, 1.f};
    particles_.push_back(p);
  }
}

void ParticleEmitter::Step(float dt) {
  if (!enabled_) {
    particles_.clear();
    return;
  }
  dt = std::max(0.f, dt);
  emit_accum_ += rate_ * dt;
  while (emit_accum_ >= 1.f) {
    EmitBurst(1);
    emit_accum_ -= 1.f;
  }
  for (auto& p : particles_) {
    p.life -= dt;
    p.velocity.y -= 6.5f * dt;
    p.position = p.position + p.velocity * dt;
    p.color.a = std::clamp(p.life / lifetime_, 0.f, 1.f);
  }
  particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                  [](const Particle& p) { return p.life <= 0.f; }),
                   particles_.end());
}

}  // namespace engine::vfx
