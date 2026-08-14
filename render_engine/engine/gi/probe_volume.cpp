#include "engine/gi/probe_volume.h"

#include <algorithm>
#include <cmath>

namespace engine::gi {

void ProbeVolume::Configure(const Vec3& origin, const Vec3& spacing, int nx, int ny, int nz) {
  origin_ = origin;
  spacing_ = spacing;
  nx_ = std::max(1, nx);
  ny_ = std::max(1, ny);
  nz_ = std::max(1, nz);
  probes_.clear();
  probes_.reserve(static_cast<std::size_t>(nx_ * ny_ * nz_));
  for (int z = 0; z < nz_; ++z) {
    for (int y = 0; y < ny_; ++y) {
      for (int x = 0; x < nx_; ++x) {
        Probe p;
        p.position = {origin_.x + x * spacing_.x, origin_.y + y * spacing_.y,
                      origin_.z + z * spacing_.z};
        const float t = static_cast<float>(x + y + z) /
                        static_cast<float>(std::max(1, nx_ + ny_ + nz_ - 3));
        p.irradiance = {0.05f + 0.2f * t, 0.05f + 0.15f * t, 0.08f + 0.1f * t, 1.f};
        probes_.push_back(p);
      }
    }
  }
}

void ProbeVolume::UpdateFromLights(std::span<const ProbeLight> lights) {
  if (probes_.empty()) {
    return;
  }
  for (auto& probe : probes_) {
    ColorRgba acc{0.05f, 0.05f, 0.06f, 1.f};
    for (const auto& light : lights) {
      const Vec3 d = probe.position - light.position;
      const float dist = d.length();
      if (dist >= light.range || light.range <= 0.f) {
        continue;
      }
      const float t = 1.f - dist / light.range;
      const float w = light.intensity * t * t;
      acc.r += light.color.r * w;
      acc.g += light.color.g * w;
      acc.b += light.color.b * w;
    }
    probe.irradiance = acc;
  }
}

ColorRgba ProbeVolume::Sample(const Vec3& world_pos) const {
  if (!enabled_ || probes_.empty()) {
    return {0, 0, 0, 1};
  }
  std::size_t best = 0;
  float best_d = 1e30f;
  for (std::size_t i = 0; i < probes_.size(); ++i) {
    const Vec3 d = probes_[i].position - world_pos;
    const float d2 = Dot(d, d);
    if (d2 < best_d) {
      best_d = d2;
      best = i;
    }
  }
  return probes_[best].irradiance;
}

}  // namespace engine::gi
