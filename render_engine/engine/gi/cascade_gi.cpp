#include "engine/gi/cascade_gi.h"

#include <algorithm>
#include <cmath>

namespace engine::gi {

void CascadeGiVolume::Configure(const CascadeGiDesc& desc) {
  desc_ = desc;
  desc_.cascade_count = std::clamp(desc_.cascade_count, 1, 3);
  desc_.base_nx = std::max(2, desc_.base_nx);
  desc_.base_ny = std::max(2, desc_.base_ny);
  desc_.base_nz = std::max(2, desc_.base_nz);
  desc_.sdf_occlusion = std::clamp(desc_.sdf_occlusion, 0.f, 1.f);
  cascades_.clear();
  cascades_.reserve(static_cast<std::size_t>(desc_.cascade_count));

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
}

void CascadeGiVolume::set_budget_per_frame(int n) {
  n = std::max(1, n);
  if (cascades_.empty()) {
    return;
  }
  const int per = std::max(1, n / static_cast<int>(cascades_.size()));
  for (auto& c : cascades_) {
    c.set_budget_per_frame(per);
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
        // Soft inside-AABB factor (Godot SDFGI spirit — not a true SDF field).
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

void CascadeGiVolume::TickProduct(std::span<const ProbeLight> lights, float neighborhood_weight) {
  if (!enabled_ || cascades_.empty()) {
    return;
  }
  for (auto& c : cascades_) {
    c.set_enabled(true);
    c.TickProduct(lights, neighborhood_weight);
  }
  ApplySdfOcclusionLite();
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
