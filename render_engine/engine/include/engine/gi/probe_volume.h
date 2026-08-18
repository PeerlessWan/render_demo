#pragma once

#include "engine/core/math.h"

#include <span>
#include <vector>

namespace engine::gi {

// M22 / W-gi-deepen: CPU probe volume (not full DDGI). Quality / F1 can disable.
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

  // W6: denser grid by subdividing each cell (keeps volume extents; rebuilds probes).
  // factor=2 → ~8× probes (2³). Clamped to [1,4].
  void RefineDensity(int factor);

  // Max probes refreshed per UpdateFromLights call (frame budget).
  void set_budget_per_frame(int n);
  [[nodiscard]] int budget_per_frame() const { return budget_per_frame_; }

  // Incremental irradiance update from dynamic lights.
  void UpdateFromLights(std::span<const ProbeLight> lights);

  // Trilinear sample of grid irradiance (clamped edges).
  [[nodiscard]] ColorRgba Sample(const Vec3& world_pos) const;
  [[nodiscard]] const std::vector<Probe>& probes() const { return probes_; }
  [[nodiscard]] std::vector<Probe>& probes() { return probes_; }
  [[nodiscard]] int grid_nx() const { return nx_; }
  [[nodiscard]] int grid_ny() const { return ny_; }
  [[nodiscard]] int grid_nz() const { return nz_; }

  // Mega-W9: flatten probe irradiance RGB into a CPU atlas (3 floats per probe, x-y-z order).
  [[nodiscard]] std::vector<float> BuildIrradianceAtlasCpu() const;
  // Sample flattened atlas with the same trilinear scheme as Sample (non-DDGI deepen).
  [[nodiscard]] ColorRgba SampleAtlasCpu(const std::vector<float>& atlas,
                                         const Vec3& world_pos) const;

  // Mega-W10 DDGI-lite (CPU only — NOT NVIDIA DDGI / RTXGI):
  // Blend each probe irradiance toward the mean of its 6-neighborhood (clamped edges).
  // weight in [0,1]: 0 = no change, 1 = full neighbor mean.
  void BlendNeighborhood(float weight = 0.25f);

  // DDGI-lite cascade densify: RefineDensity near `focus` by subdividing the whole grid
  // `levels` times (each level factor=2). Keeps extents; not a multi-volume cascade stack.
  void CascadeRefine(const Vec3& focus, int levels = 1);

 private:
  bool enabled_ = true;
  Vec3 origin_{};
  Vec3 spacing_{1, 1, 1};
  int nx_ = 0, ny_ = 0, nz_ = 0;
  int budget_per_frame_ = 32;
  int update_cursor_ = 0;
  std::vector<Probe> probes_;
};

}  // namespace engine::gi
