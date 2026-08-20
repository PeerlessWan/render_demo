#include "engine/gi/cascade_gi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::gi {

void CascadeGiVolume::Configure(const CascadeGiDesc& desc) {
  desc_ = desc;
  desc_.cascade_count = std::clamp(desc_.cascade_count, 1, 3);
  desc_.base_nx = std::max(2, desc_.base_nx);
  desc_.base_ny = std::max(2, desc_.base_ny);
  desc_.base_nz = std::max(2, desc_.base_nz);
  desc_.sdf_occlusion = std::clamp(desc_.sdf_occlusion, 0.f, 1.f);
  desc_.leak_suppress = std::clamp(desc_.leak_suppress, 0.f, 1.f);
  cascades_.clear();
  cascades_.reserve(static_cast<std::size_t>(desc_.cascade_count));
  tick_ = 0;

  for (int c = 0; c < desc_.cascade_count; ++c) {
    const float grow = std::pow(2.f, static_cast<float>(c));
    const Vec3 origin = desc_.origin - desc_.extent * (grow - 1.f) * 0.5f;
    const Vec3 extent = desc_.extent * grow;
    const int nx = std::max(2, desc_.base_nx / (1 + c));
    const int ny = std::max(2, desc_.base_ny / (1 + c));
    const int nz = std::max(2, desc_.base_nz / (1 + c));
    const Vec3 spacing{extent.x / static_cast<float>(std::max(1, nx - 1)),
                       extent.y / static_cast<float>(std::max(1, ny - 1)),
                       extent.z / static_cast<float>(std::max(1, nz - 1))};
    ProbeVolume vol;
    vol.Configure(origin, spacing, nx, ny, nz);
    cascades_.push_back(std::move(vol));
  }
  set_budget_per_frame(total_budget_);
}

void CascadeGiVolume::set_budget_per_frame(int n) {
  total_budget_ = std::max(1, n);
  if (cascades_.empty()) {
    return;
  }
  // Near cascade gets ~half budget; remainder split across far cascades.
  const int near_b = std::max(1, total_budget_ / 2);
  cascades_.front().set_budget_per_frame(near_b);
  const int rest = std::max(1, total_budget_ - near_b);
  const int far_n = std::max(1, static_cast<int>(cascades_.size()) - 1);
  const int per_far = std::max(1, rest / far_n);
  for (std::size_t i = 1; i < cascades_.size(); ++i) {
    cascades_[i].set_budget_per_frame(per_far);
  }
}

void CascadeGiVolume::set_occluders(std::span<const CascadeOccluderAabb> boxes) {
  occluders_.assign(boxes.begin(), boxes.end());
}

void CascadeGiVolume::ApplySdfOcclusionLite() {
  const float w = desc_.sdf_occlusion;
  if (w <= 1e-4f || occluders_.empty()) {
    return;
  }
  for (auto& cascade : cascades_) {
    for (auto& probe : cascade.probes()) {
      float occ = 0.f;
      for (const auto& box : occluders_) {
        const float dx = std::max(box.min_p.x - probe.position.x, probe.position.x - box.max_p.x);
        const float dy = std::max(box.min_p.y - probe.position.y, probe.position.y - box.max_p.y);
        const float dz = std::max(box.min_p.z - probe.position.z, probe.position.z - box.max_p.z);
        const float outside = std::max(dx, std::max(dy, dz));
        if (outside <= 0.f) {
          occ = std::max(occ, 0.85f);
        } else {
          const float soft = std::clamp(1.f - outside / 1.5f, 0.f, 1.f);
          occ = std::max(occ, soft * 0.55f);
        }
      }
      if (occ > 0.f) {
        const float atten = 1.f - occ * w;
        probe.irradiance.r *= atten;
        probe.irradiance.g *= atten;
        probe.irradiance.b *= atten;
      }
    }
  }
}

void CascadeGiVolume::ApplyLeakSuppress() {
  const float leak = desc_.leak_suppress;
  if (leak <= 1e-4f || occluders_.empty()) {
    return;
  }
  for (auto& cascade : cascades_) {
    for (auto& probe : cascade.probes()) {
      float near_wall = 0.f;
      for (const auto& box : occluders_) {
        const float cx = std::clamp(probe.position.x, box.min_p.x, box.max_p.x);
        const float cy = std::clamp(probe.position.y, box.min_p.y, box.max_p.y);
        const float cz = std::clamp(probe.position.z, box.min_p.z, box.max_p.z);
        const float dx = probe.position.x - cx;
        const float dy = probe.position.y - cy;
        const float dz = probe.position.z - cz;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        near_wall = std::max(near_wall, std::clamp(1.f - dist / 0.75f, 0.f, 1.f));
      }
      if (near_wall > 0.f) {
        const float atten = 1.f - near_wall * leak * 0.55f;
        probe.irradiance.r *= atten;
        probe.irradiance.g *= atten;
        probe.irradiance.b *= atten;
      }
    }
  }
}

void CascadeGiVolume::TickProduct(std::span<const ProbeLight> lights, float neighborhood_weight) {
  if (!enabled_ || cascades_.empty()) {
    return;
  }
  ++tick_;
  for (std::size_t i = 0; i < cascades_.size(); ++i) {
    // Cascade 0 every frame; cascade k every (k+1) frames.
    const std::uint64_t period = static_cast<std::uint64_t>(i) + 1u;
    if ((tick_ % period) != 0u && i > 0) {
      continue;
    }
    cascades_[i].set_enabled(true);
    cascades_[i].TickProduct(lights, neighborhood_weight);
  }
  ApplySdfOcclusionLite();
  ApplyLeakSuppress();
}

ColorRgba CascadeGiVolume::BlendWithReflection(const ColorRgba& probe_gi, const ColorRgba& reflection,
                                               float weight) {
  weight = std::clamp(weight, 0.f, 1.f);
  return ColorRgba{probe_gi.r + (reflection.r - probe_gi.r) * weight,
                   probe_gi.g + (reflection.g - probe_gi.g) * weight,
                   probe_gi.b + (reflection.b - probe_gi.b) * weight, 1.f};
}

ColorRgba CascadeGiVolume::Sample(const Vec3& world_pos) const {
  if (!enabled_ || cascades_.empty()) {
    return {0, 0, 0, 1};
  }
  for (const auto& c : cascades_) {
    const Vec3 o = c.origin();
    const Vec3 sp = c.spacing();
    const float max_x = o.x + sp.x * static_cast<float>(std::max(0, c.grid_nx() - 1));
    const float max_y = o.y + sp.y * static_cast<float>(std::max(0, c.grid_ny() - 1));
    const float max_z = o.z + sp.z * static_cast<float>(std::max(0, c.grid_nz() - 1));
    if (world_pos.x >= o.x && world_pos.x <= max_x && world_pos.y >= o.y && world_pos.y <= max_y &&
        world_pos.z >= o.z && world_pos.z <= max_z) {
      return c.Sample(world_pos);
    }
  }
  return cascades_.back().Sample(world_pos);
}

std::vector<float> CascadeGiVolume::BuildIrradianceAtlasCpu() const {
  if (cascades_.empty()) {
    return {};
  }
  return cascades_.front().BuildIrradianceAtlasCpu();
}

}  // namespace engine::gi
