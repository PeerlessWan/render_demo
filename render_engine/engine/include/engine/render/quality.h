#pragma once

namespace engine::render {

enum class QualityTier { Low, Medium, High };

struct QualitySettings {
  QualityTier tier = QualityTier::Medium;
  int shadow_cascades = 2;
  int shadow_atlas_size = 2048;
  float max_shadow_distance = 80.f;
  int probe_update_budget = 64;
  int vegetation_cap = 24;
  bool enable_bloom = true;
  bool enable_ssao = true;
  bool enable_taa = false;
  bool enable_ssr = false;
  bool enable_dof = false;
  bool enable_motion_blur = false;
  bool enable_raytracing = false;
  bool multithread_submit = false;
  // W22 Low-tier weak-GPU gates (ADR 0045)
  bool enable_cascade_gi = true;
  bool enable_volumetric_fog = true;
  bool enable_soft_shadow = true;
  // W24 / ADR 0047 domain seal
  bool enable_gtao = false;
  bool enable_virtual_geometry = true;
  bool enable_rt_reflection = false;

  static QualitySettings FromTier(QualityTier t);
};

}  // namespace engine::render
