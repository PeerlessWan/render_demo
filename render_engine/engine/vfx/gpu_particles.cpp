#include "engine/vfx/gpu_particles.h"
#include "engine/vfx/gpu_particles_internal.h"

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
  sub_accum_ = 0.f;
  last_path_ = "configured";
  last_indirect_ = {};
  trail_.Configure(0.45f, 0.06f, 48);
  trail_.Clear();
}

void GpuParticleSystem::set_trail_enabled(bool on) {
  trail_enabled_ = on;
  trail_.set_enabled(on);
  if (!on) {
    trail_.Clear();
  }
}

void GpuParticleSystem::ApplyAttractor(float dt) {
  if (!attractor_.enabled || attractor_.radius <= 1e-4f) {
    return;
  }
  for (auto& p : particles_) {
    Vec3 d = attractor_.position - p.position;
    const float dist = d.length();
    if (dist >= attractor_.radius || dist < 1e-4f) {
      continue;
    }
    const float t = 1.f - dist / attractor_.radius;
    const float w = attractor_.strength * t * t * dt;
    d = d * (1.f / dist);
    p.velocity = p.velocity + d * w;
  }
}

void GpuParticleSystem::UpdateTrail() {
  if (!trail_enabled_) {
    return;
  }
  trail_.Step(1.f / 60.f);
  if (!particles_.empty()) {
    trail_.Push(particles_.front().position, particles_.front().color);
  }
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

void GpuParticleSystem::EmitSubCpu(const Particle& parent, int count) {
  for (int i = 0; i < count; ++i) {
    if (particles_.size() >= max_particles_) {
      break;
    }
    Particle p;
    p.position = parent.position;
    p.velocity = {NextRand() * 0.8f - 0.4f, NextRand() * 0.6f, NextRand() * 0.8f - 0.4f};
    p.life = std::max(0.05f, sub_emit_.lifetime);
    p.size = std::max(0.5f, parent.size * sub_emit_.size_scale);
    p.color = {parent.color.r, parent.color.g * 0.85f, parent.color.b * 0.6f, parent.color.a * 0.8f};
    particles_.push_back(p);
  }
}

void GpuParticleSystem::ApplyCollisionAndKill() {
  if (collision_.enabled) {
    Vec3 n = collision_.normal;
    const float nlen = n.length();
    if (nlen > 1e-4f) {
      n = n * (1.f / nlen);
      for (auto& p : particles_) {
        const Vec3 to_p = p.position - collision_.point;
        const float dist = to_p.x * n.x + to_p.y * n.y + to_p.z * n.z;
        if (dist < 0.f) {
          p.position = p.position - n * dist;
          const float vn = p.velocity.x * n.x + p.velocity.y * n.y + p.velocity.z * n.z;
          if (vn < 0.f) {
            p.velocity = p.velocity - n * (vn * (1.f + std::clamp(collision_.bounce, 0.f, 1.f)));
          }
        }
      }
    }
  }
  if (kill_box_.enabled) {
    for (auto& p : particles_) {
      if (p.position.x < kill_box_.min_p.x || p.position.y < kill_box_.min_p.y ||
          p.position.z < kill_box_.min_p.z || p.position.x > kill_box_.max_p.x ||
          p.position.y > kill_box_.max_p.y || p.position.z > kill_box_.max_p.z) {
        p.life = 0.f;
      }
    }
  }
}

void GpuParticleSystem::IntegrateCpu(float dt) {
  std::vector<Particle> parents_for_sub;
  if (sub_emit_.enabled && sub_emit_.rate > 0.f) {
    parents_for_sub.reserve(particles_.size());
  }
  for (auto& p : particles_) {
    p.life -= dt;
    p.position = p.position + p.velocity * dt;
    p.velocity.y -= 2.5f * dt;
    if (sub_emit_.enabled && p.life > 0.f) {
      parents_for_sub.push_back(p);
    }
  }
  ApplyAttractor(dt);
  ApplyCollisionAndKill();
  if (sub_emit_.enabled && !parents_for_sub.empty()) {
    sub_accum_ += sub_emit_.rate * dt * static_cast<float>(parents_for_sub.size()) * 0.05f;
    const int burst = static_cast<int>(sub_accum_);
    if (burst > 0) {
      sub_accum_ -= static_cast<float>(burst);
      const auto& parent = parents_for_sub[static_cast<std::size_t>(rng_ % parents_for_sub.size())];
      EmitSubCpu(parent, std::min(burst, 8));
    }
  }
  CullDead();
}

void GpuParticleSystem::CullDead() {
  particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                  [](const Particle& p) { return p.life <= 0.f; }),
                   particles_.end());
}

void GpuParticleSystem::FillIndirect() {
  last_indirect_.index_count_per_instance = static_cast<std::uint32_t>(particles_.size());
  last_indirect_.instance_count = 1;
  last_indirect_.start_index_location = 0;
  last_indirect_.base_vertex_location = 0;
  last_indirect_.start_instance_location = 0;
}

Status GpuParticleSystem::Step(float dt) {
  if (!enabled_) {
    last_path_ = "disabled";
    last_indirect_ = {};
    return Status::Ok("disabled");
  }
  dt = std::max(0.f, dt);
  emit_accum_ += rate_ * dt;
  const int burst = static_cast<int>(emit_accum_);
  if (burst > 0) {
    emit_accum_ -= static_cast<float>(burst);
    EmitCpu(burst);
  }

  // W17 ADR 0041: Feature gpu_particles → try D3D12 CS then Vulkan CS; else cpu-fallback.
  if (QueryFeature("gpu_particles")) {
    Status gpu = particles_detail::TryIntegrateGpuCsD3d12(particles_, dt);
    if (!gpu) {
      gpu = particles_detail::TryIntegrateGpuCsVk(particles_, dt);
    }
    if (gpu) {
      ApplyAttractor(dt);
      ApplyCollisionAndKill();
      CullDead();
      FillIndirect();
      UpdateTrail();
      last_path_ = gpu.message().empty() ? "gpu-cs" : gpu.message();
      return Status::Ok(last_path_.c_str());
    }
    IntegrateCpu(dt);
    FillIndirect();
    UpdateTrail();
    last_path_ = "cpu-fallback";
    static bool once = false;
    if (!once) {
      once = true;
      LogInfo(std::string("GpuParticleSystem: CS unavailable (") + gpu.message() +
              "); cpu-fallback (ADR 0041)");
    }
    return Status::Ok("cpu-fallback");
  }

  IntegrateCpu(dt);
  FillIndirect();
  UpdateTrail();
  last_path_ = "cpu-fallback";
  return Status::Ok("cpu-fallback");
}

}  // namespace engine::vfx
