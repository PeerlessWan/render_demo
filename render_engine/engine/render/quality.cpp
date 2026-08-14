#include "engine/render/quality.h"

namespace engine::render {

QualitySettings QualitySettings::FromTier(QualityTier t) {
  QualitySettings q;
  q.tier = t;
  switch (t) {
    case QualityTier::Low:
      q.shadow_cascades = 1;
      q.enable_bloom = false;
      q.enable_ssao = false;
      q.enable_taa = false;
      q.enable_ssr = false;
      break;
    case QualityTier::Medium:
      q.shadow_cascades = 2;
      q.enable_bloom = true;
      q.enable_ssao = true;
      q.enable_taa = true;  // motion-vector reprojection available
      q.enable_ssr = false;
      break;
    case QualityTier::High:
      q.shadow_cascades = 4;
      q.enable_bloom = true;
      q.enable_ssao = true;
      q.enable_taa = true;
      q.enable_ssr = false;  // SSR sparkle on floors until more stable
      break;
  }
  return q;
}

}  // namespace engine::render
