#include "engine/render/quality.h"

namespace engine::render {

QualitySettings QualitySettings::FromTier(QualityTier t) {
  QualitySettings q;
  q.tier = t;
  switch (t) {
    case QualityTier::Low:
      q.shadow_cascades = 1;
      q.shadow_atlas_size = 1024;
      q.max_shadow_distance = 40.f;
      q.probe_update_budget = 16;
      q.vegetation_cap = 8;
      q.enable_bloom = false;
      q.enable_ssao = false;
      q.enable_taa = false;
      q.enable_ssr = false;
      q.enable_dof = false;
      q.enable_motion_blur = false;
      q.enable_cascade_gi = false;
      q.enable_volumetric_fog = false;
      q.enable_soft_shadow = false;
      q.enable_gtao = false;
      q.enable_virtual_geometry = false;
      q.enable_rt_reflection = false;
      break;
    case QualityTier::Medium:
      q.shadow_cascades = 2;
      q.shadow_atlas_size = 2048;
      q.max_shadow_distance = 80.f;
      q.probe_update_budget = 64;
      q.vegetation_cap = 24;
      q.enable_bloom = true;
      q.enable_ssao = true;
      q.enable_taa = true;  // Quality default; Sandbox may force FX TAA off for FPS
      q.enable_ssr = true;
      q.enable_dof = false;
      q.enable_motion_blur = false;
      q.enable_cascade_gi = true;
      q.enable_volumetric_fog = true;
      q.enable_soft_shadow = true;
      q.enable_gtao = true;
      q.enable_virtual_geometry = true;
      q.enable_rt_reflection = false;
      break;
    case QualityTier::High:
      q.shadow_cascades = 4;
      q.shadow_atlas_size = 2048;
      q.max_shadow_distance = 120.f;
      q.probe_update_budget = 128;
      q.vegetation_cap = 48;
      q.enable_bloom = true;
      q.enable_ssao = true;
      q.enable_taa = true;
      q.enable_ssr = true;
      q.enable_dof = true;
      q.enable_motion_blur = false;
      q.enable_raytracing = true;
      q.enable_cascade_gi = true;
      q.enable_volumetric_fog = true;
      q.enable_soft_shadow = true;
      q.enable_gtao = true;
      q.enable_virtual_geometry = true;
      q.enable_rt_reflection = true;
      break;
  }
  return q;
}

}  // namespace engine::render
