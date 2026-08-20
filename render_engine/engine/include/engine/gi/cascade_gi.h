#pragma once

#include "engine/gi/probe_volume.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::gi {

// W21–W22 / ADR 0044–0045: Godot SDFGI-style cascade stack (NOT NVIDIA DDGI / Lumen / RTXGI).
struct CascadeGiDesc {
  Vec3 origin{-4.f, 0.f, -4.f};
  Vec3 extent{8.f, 4.f, 8.f};
  int cascade_count = 2;       // clamped [1,3]
  int base_nx = 6;
  int base_ny = 4;
  int base_nz = 6;
  float sdf_occlusion = 0.35f; // [0,1] soft occlusion weight
  float leak_suppress = 0.25f;  // W22: extra damp near occluder surfaces
};

struct CascadeOccluderAabb {
  Vec3 min_p{};
  Vec3 max_p{};
};

class CascadeGiVolume {
 public:
  void Configure(const CascadeGiDesc& desc);
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }

  void set_budget_per_frame(int n);
  void set_occluders(std::span<const CascadeOccluderAabb> boxes);

  // W22: near cascade every tick; far cascades every (index+1) frames.
  void TickProduct(std::span<const ProbeLight> lights, float neighborhood_weight = 0.18f);

  // Blend Probe GI sample with reflection-probe color (CPU product weight).
  [[nodiscard]] static ColorRgba BlendWithReflection(const ColorRgba& probe_gi,
                                                     const ColorRgba& reflection, float weight);

  [[nodiscard]] ColorRgba Sample(const Vec3& world_pos) const;
  [[nodiscard]] std::vector<float> BuildIrradianceAtlasCpu() const;
  [[nodiscard]] const ProbeVolume& primary() const {
    return cascades_.empty() ? empty_ : cascades_.front();
  }
  [[nodiscard]] ProbeVolume& primary() {
    if (cascades_.empty()) {
      cascades_.emplace_back();
    }
    return cascades_.front();
  }
  [[nodiscard]] int cascade_count() const { return static_cast<int>(cascades_.size()); }
  [[nodiscard]] const std::vector<ProbeVolume>& cascades() const { return cascades_; }
  [[nodiscard]] std::uint64_t tick_index() const { return tick_; }

 private:
  bool enabled_ = true;
  CascadeGiDesc desc_{};
  std::vector<ProbeVolume> cascades_;
  std::vector<CascadeOccluderAabb> occluders_;
  ProbeVolume empty_{};
  std::uint64_t tick_ = 0;
  int total_budget_ = 64;

  void ApplySdfOcclusionLite();
  void ApplyLeakSuppress();
};

}  // namespace engine::gi
