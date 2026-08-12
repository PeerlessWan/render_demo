#pragma once

#include "engine/core/math.h"

#include <vector>

namespace engine::render {

struct CsmCascade {
  float split = 0.f;
  Mat4 view_proj = Mat4::Identity();
};

class CascadedShadowMap {
 public:
  void set_cascade_count(int n);
  void Build(const Vec3& light_dir, float z_near, float z_far);
  [[nodiscard]] const std::vector<CsmCascade>& cascades() const { return cascades_; }

  static std::vector<float> ComputeSplits(float z_near, float z_far, int count,
                                          float lambda = 0.5f);

 private:
  int count_ = 2;
  std::vector<CsmCascade> cascades_;
};

}  // namespace engine::render
