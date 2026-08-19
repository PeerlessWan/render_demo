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
  update_cursor_ = 0;
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

void ProbeVolume::RefineDensity(int factor) {
  factor = std::clamp(factor, 1, 4);
  if (factor <= 1 || nx_ <= 0 || ny_ <= 0 || nz_ <= 0) {
    return;
  }
  const Vec3 extent{spacing_.x * static_cast<float>(std::max(1, nx_ - 1)),
                    spacing_.y * static_cast<float>(std::max(1, ny_ - 1)),
                    spacing_.z * static_cast<float>(std::max(1, nz_ - 1))};
  const int nnx = (nx_ - 1) * factor + 1;
  const int nny = (ny_ - 1) * factor + 1;
  const int nnz = (nz_ - 1) * factor + 1;
  const Vec3 nsp{extent.x / static_cast<float>(std::max(1, nnx - 1)),
                 extent.y / static_cast<float>(std::max(1, nny - 1)),
                 extent.z / static_cast<float>(std::max(1, nnz - 1))};
  Configure(origin_, nsp, nnx, nny, nnz);
  // Denser grids need a higher refresh budget so irradiance converges in similar wall time.
  set_budget_per_frame(budget_per_frame_ * factor * factor);
}

void ProbeVolume::set_budget_per_frame(int n) {
  budget_per_frame_ = std::max(1, n);
}

void ProbeVolume::UpdateFromLights(std::span<const ProbeLight> lights) {
  if (probes_.empty()) {
    return;
  }
  const int total = static_cast<int>(probes_.size());
  const int count = (std::min)(budget_per_frame_, total);
  for (int k = 0; k < count; ++k) {
    const int idx = (update_cursor_ + k) % total;
    auto& probe = probes_[static_cast<std::size_t>(idx)];
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
    // Temporal blend so partial updates don't flicker.
    probe.irradiance.r = probe.irradiance.r * 0.35f + acc.r * 0.65f;
    probe.irradiance.g = probe.irradiance.g * 0.35f + acc.g * 0.65f;
    probe.irradiance.b = probe.irradiance.b * 0.35f + acc.b * 0.65f;
  }
  update_cursor_ = (update_cursor_ + count) % total;
}

ColorRgba ProbeVolume::Sample(const Vec3& world_pos) const {
  if (!enabled_ || probes_.empty()) {
    return {0, 0, 0, 1};
  }
  // Trilinear blend of the 8 surrounding grid cells (clamped).
  const float fx = (world_pos.x - origin_.x) / std::max(spacing_.x, 1e-4f);
  const float fy = (world_pos.y - origin_.y) / std::max(spacing_.y, 1e-4f);
  const float fz = (world_pos.z - origin_.z) / std::max(spacing_.z, 1e-4f);
  const int x0 = (std::max)(0, (std::min)(nx_ - 1, static_cast<int>(std::floor(fx))));
  const int y0 = (std::max)(0, (std::min)(ny_ - 1, static_cast<int>(std::floor(fy))));
  const int z0 = (std::max)(0, (std::min)(nz_ - 1, static_cast<int>(std::floor(fz))));
  const int x1 = (std::min)(nx_ - 1, x0 + 1);
  const int y1 = (std::min)(ny_ - 1, y0 + 1);
  const int z1 = (std::min)(nz_ - 1, z0 + 1);
  const float tx = (std::max)(0.f, (std::min)(1.f, fx - static_cast<float>(x0)));
  const float ty = (std::max)(0.f, (std::min)(1.f, fy - static_cast<float>(y0)));
  const float tz = (std::max)(0.f, (std::min)(1.f, fz - static_cast<float>(z0)));

  auto at = [&](int x, int y, int z) -> const ColorRgba& {
    const int i = x + nx_ * (y + ny_ * z);
    return probes_[static_cast<std::size_t>(i)].irradiance;
  };
  auto lerp4 = [](const ColorRgba& a, const ColorRgba& b, float t) {
    return ColorRgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
  };
  const ColorRgba c00 = lerp4(at(x0, y0, z0), at(x1, y0, z0), tx);
  const ColorRgba c10 = lerp4(at(x0, y1, z0), at(x1, y1, z0), tx);
  const ColorRgba c01 = lerp4(at(x0, y0, z1), at(x1, y0, z1), tx);
  const ColorRgba c11 = lerp4(at(x0, y1, z1), at(x1, y1, z1), tx);
  const ColorRgba c0 = lerp4(c00, c10, ty);
  const ColorRgba c1 = lerp4(c01, c11, ty);
  return lerp4(c0, c1, tz);
}

std::vector<float> ProbeVolume::BuildIrradianceAtlasCpu() const {
  std::vector<float> atlas;
  atlas.reserve(probes_.size() * 3);
  for (const auto& p : probes_) {
    atlas.push_back(p.irradiance.r);
    atlas.push_back(p.irradiance.g);
    atlas.push_back(p.irradiance.b);
  }
  return atlas;
}

ColorRgba ProbeVolume::SampleAtlasCpu(const std::vector<float>& atlas,
                                      const Vec3& world_pos) const {
  if (!enabled_ || probes_.empty() || atlas.size() < probes_.size() * 3) {
    return {0, 0, 0, 1};
  }
  const float fx = (world_pos.x - origin_.x) / std::max(spacing_.x, 1e-4f);
  const float fy = (world_pos.y - origin_.y) / std::max(spacing_.y, 1e-4f);
  const float fz = (world_pos.z - origin_.z) / std::max(spacing_.z, 1e-4f);
  const int x0 = (std::max)(0, (std::min)(nx_ - 1, static_cast<int>(std::floor(fx))));
  const int y0 = (std::max)(0, (std::min)(ny_ - 1, static_cast<int>(std::floor(fy))));
  const int z0 = (std::max)(0, (std::min)(nz_ - 1, static_cast<int>(std::floor(fz))));
  const int x1 = (std::min)(nx_ - 1, x0 + 1);
  const int y1 = (std::min)(ny_ - 1, y0 + 1);
  const int z1 = (std::min)(nz_ - 1, z0 + 1);
  const float tx = (std::max)(0.f, (std::min)(1.f, fx - static_cast<float>(x0)));
  const float ty = (std::max)(0.f, (std::min)(1.f, fy - static_cast<float>(y0)));
  const float tz = (std::max)(0.f, (std::min)(1.f, fz - static_cast<float>(z0)));

  auto at = [&](int x, int y, int z) -> ColorRgba {
    const int i = x + nx_ * (y + ny_ * z);
    const std::size_t base = static_cast<std::size_t>(i) * 3;
    return ColorRgba{atlas[base], atlas[base + 1], atlas[base + 2], 1.f};
  };
  auto lerp4 = [](const ColorRgba& a, const ColorRgba& b, float t) {
    return ColorRgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
  };
  const ColorRgba c00 = lerp4(at(x0, y0, z0), at(x1, y0, z0), tx);
  const ColorRgba c10 = lerp4(at(x0, y1, z0), at(x1, y1, z0), tx);
  const ColorRgba c01 = lerp4(at(x0, y0, z1), at(x1, y0, z1), tx);
  const ColorRgba c11 = lerp4(at(x0, y1, z1), at(x1, y1, z1), tx);
  const ColorRgba c0 = lerp4(c00, c10, ty);
  const ColorRgba c1 = lerp4(c01, c11, ty);
  return lerp4(c0, c1, tz);
}

void ProbeVolume::BlendNeighborhood(float weight) {
  weight = std::clamp(weight, 0.f, 1.f);
  if (weight <= 0.f || probes_.empty() || nx_ <= 0 || ny_ <= 0 || nz_ <= 0) {
    return;
  }
  auto idx = [&](int x, int y, int z) {
    return static_cast<std::size_t>(x + nx_ * (y + ny_ * z));
  };
  std::vector<ColorRgba> next(probes_.size());
  for (int z = 0; z < nz_; ++z) {
    for (int y = 0; y < ny_; ++y) {
      for (int x = 0; x < nx_; ++x) {
        const auto i = idx(x, y, z);
        ColorRgba sum{};
        int n = 0;
        auto add = [&](int xx, int yy, int zz) {
          if (xx < 0 || yy < 0 || zz < 0 || xx >= nx_ || yy >= ny_ || zz >= nz_) {
            return;
          }
          const auto& c = probes_[idx(xx, yy, zz)].irradiance;
          sum.r += c.r;
          sum.g += c.g;
          sum.b += c.b;
          ++n;
        };
        add(x - 1, y, z);
        add(x + 1, y, z);
        add(x, y - 1, z);
        add(x, y + 1, z);
        add(x, y, z - 1);
        add(x, y, z + 1);
        const ColorRgba& self = probes_[i].irradiance;
        if (n == 0) {
          next[i] = self;
          continue;
        }
        const float inv = 1.f / static_cast<float>(n);
        const ColorRgba mean{sum.r * inv, sum.g * inv, sum.b * inv, 1.f};
        next[i] = ColorRgba{self.r + (mean.r - self.r) * weight,
                            self.g + (mean.g - self.g) * weight,
                            self.b + (mean.b - self.b) * weight, 1.f};
      }
    }
  }
  for (std::size_t i = 0; i < probes_.size(); ++i) {
    probes_[i].irradiance = next[i];
  }
}

void ProbeVolume::CascadeRefine(const Vec3& focus, int levels) {
  levels = std::clamp(levels, 0, 3);
  if (levels <= 0 || nx_ <= 0) {
    return;
  }
  // focus is documented for callers (camera / character); densify is uniform for lite path.
  (void)focus;
  for (int i = 0; i < levels; ++i) {
    RefineDensity(2);
  }
}

void ProbeVolume::TickProduct(std::span<const ProbeLight> lights, float neighborhood_weight) {
  if (!enabled_ || probes_.empty()) {
    return;
  }
  UpdateFromLights(lights);
  const float w = std::clamp(neighborhood_weight, 0.f, 1.f);
  if (w > 1e-4f) {
    BlendNeighborhood(w);
  }
}

}  // namespace engine::gi
