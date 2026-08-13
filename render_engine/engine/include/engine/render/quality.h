#pragma once

namespace engine::render {

enum class QualityTier { Low, Medium, High };

struct QualitySettings {
  QualityTier tier = QualityTier::Medium;
  int shadow_cascades = 2;
  bool enable_bloom = true;
  bool enable_ssao = true;
  bool enable_taa = false;
  bool enable_ssr = false;
  bool enable_raytracing = false;
  bool multithread_submit = false;

  static QualitySettings FromTier(QualityTier t);
};

}  // namespace engine::render
