#include "engine/render/shadow_csm.h"

#include <cmath>

namespace engine::render {

void CascadedShadowMap::set_cascade_count(int n) {
  count_ = n < 1 ? 1 : (n > 4 ? 4 : n);
}

std::vector<float> CascadedShadowMap::ComputeSplits(float z_near, float z_far, int count,
                                                    float lambda) {
  std::vector<float> splits;
  for (int i = 1; i <= count; ++i) {
    const float p = static_cast<float>(i) / static_cast<float>(count);
    const float log_s = z_near * std::pow(z_far / z_near, p);
    const float uni_s = z_near + (z_far - z_near) * p;
    splits.push_back(lambda * log_s + (1.f - lambda) * uni_s);
  }
  return splits;
}

void CascadedShadowMap::Build(const Vec3& light_dir, float z_near, float z_far) {
  cascades_.clear();
  const auto splits = ComputeSplits(z_near, z_far, count_);
  const Vec3 dir = Normalize(light_dir);
  for (float s : splits) {
    CsmCascade c;
    c.split = s;
    const Vec3 eye = dir * -50.f;
    c.view_proj = Mat4::LookAt(eye, Vec3{}, Vec3{0, 1, 0});
    cascades_.push_back(c);
  }
}

}  // namespace engine::render
