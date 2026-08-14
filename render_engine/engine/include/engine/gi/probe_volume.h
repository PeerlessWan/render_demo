#pragma once

#include "engine/core/math.h"

#include <span>
#include <vector>

namespace engine::gi {

// M22: dynamic probe volume (CPU stub). Quality can disable.
struct Probe {
  Vec3 position{};
  ColorRgba irradiance{0.1f, 0.1f, 0.12f, 1.f};
};

struct ProbeLight {
  Vec3 position{};
  ColorRgba color{1.f, 1.f, 1.f, 1.f};
  float intensity = 1.f;
  float range = 5.f;
};

class ProbeVolume {
 public:
  void Configure(const Vec3& origin, const Vec3& spacing, int nx, int ny, int nz);
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }

  // Recompute probe irradiance from dynamic lights (CPU stub).
  void UpdateFromLights(std::span<const ProbeLight> lights);

  // Sample trilinear-ish nearest probe irradiance.
  [[nodiscard]] ColorRgba Sample(const Vec3& world_pos) const;
  [[nodiscard]] const std::vector<Probe>& probes() const { return probes_; }

 private:
  bool enabled_ = true;
  Vec3 origin_{};
  Vec3 spacing_{1, 1, 1};
  int nx_ = 0, ny_ = 0, nz_ = 0;
  std::vector<Probe> probes_;
};

}  // namespace engine::gi
