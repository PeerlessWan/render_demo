#include "engine/vfx/gpu_particles.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>

namespace engine::vfx {

void GpuParticleSystem::Configure(const Vec3& origin, float rate, float lifetime,
                                  std::uint32_t max_particles) {
  origin_ = origin;
  rate_ = std::max(0.f, rate);
  lifetime_ = std::max(0.05f, lifetime);
  max_particles_ = std::max(1u, max_particles);
  particles_.clear();
  emit_accum_ = 0.f;
  last_path_ = "configured";
}

float GpuParticleSystem::NextRand() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

void GpuParticleSystem::EmitCpu(int count) {
  for (int i = 0; i < count; ++i) {
    if (particles_.size() >= max_particles_) {
      break;
    }
    Particle p;
    p.position = origin_;
    p.velocity = {NextRand() * 0.6f - 0.3f, 1.2f + NextRand() * 0.8f, NextRand() * 0.6f - 0.3f};
    p.life = lifetime_;
    p.size = 3.f + NextRand() * 3.f;
    p.color = {1.f, 0.65f + NextRand() * 0.2f, 0.25f, 1.f};
    particles_.push_back(p);
  }
}

void GpuParticleSystem::IntegrateCpu(float dt) {
  for (auto& p : particles_) {
    p.life -= dt;
    p.position = p.position + p.velocity * dt;
    p.velocity.y -= 2.5f * dt;
  }
  particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                  [](const Particle& p) { return p.life <= 0.f; }),
                   particles_.end());
}

Status GpuParticleSystem::Step(float dt) {
  if (!enabled_) {
    last_path_ = "disabled";
    return Status::Ok("disabled");
  }
  dt = std::max(0.f, dt);
  emit_accum_ += rate_ * dt;
  const int burst = static_cast<int>(emit_accum_);
  if (burst > 0) {
    emit_accum_ -= static_cast<float>(burst);
    EmitCpu(burst);
  }

  // Product path: Feature gpu_particles opts into "GPU" contract. Until CS buffers ship,
  // integrate on CPU but label path honestly (ADR 0039 — no fake GPU success).
  if (QueryFeature("gpu_particles")) {
    IntegrateCpu(dt);
    last_path_ = "gpu-contract-cpu-integrate";
    static bool once = false;
    if (!once) {
      once = true;
      LogInfo("GpuParticleSystem: Feature gpu_particles on — CS buffer path pending; "
              "CPU integrate (honest)");
    }
    return Status::Ok(last_path_.c_str());
  }

  IntegrateCpu(dt);
  last_path_ = "cpu-fallback";
  return Status::Ok("cpu-fallback");
}

}  // namespace engine::vfx
