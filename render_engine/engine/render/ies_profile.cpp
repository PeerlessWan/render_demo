#include "engine/render/ies_profile.h"

#include <algorithm>
#include <cmath>

namespace engine::render {
namespace {

float Saturate(float x) { return std::clamp(x, 0.f, 1.f); }

}  // namespace

float SampleIesLut(float u, int profile) {
  u = Saturate(u);
  switch (profile) {
    case 1:  // narrow
      return std::pow(1.f - u, 4.5f);
    case 2:  // wide wash
      return std::pow(1.f - u, 1.6f);
    case 3: {  // batwing: dip on-axis, peak mid
      const float mid = 1.f - std::fabs(u - 0.45f) * 2.2f;
      return Saturate(mid) * (0.35f + 0.65f * (1.f - u));
    }
    default:
      return 1.f;
  }
}

float EvalIesFactor(float cos_theta, int profile) {
  if (profile <= 0) {
    return 1.f;
  }
  const float c = Saturate(cos_theta);
  const float u = 1.f - c;  // 0 along axis
  return SampleIesLut(u, profile);
}

}  // namespace engine::render
