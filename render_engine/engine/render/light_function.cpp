#include "engine/render/light_function.h"

#include <algorithm>

namespace engine::render {

LightFunctionProfile ParseLightFunctionId(std::string_view id) {
  if (id.empty() || id == "off" || id == "none") {
    return LightFunctionProfile::Off;
  }
  if (id == "soft_disk" || id == "1") {
    return LightFunctionProfile::SoftDisk;
  }
  if (id == "radial" || id == "radial_falloff" || id == "2") {
    return LightFunctionProfile::RadialFalloff;
  }
  if (id == "angle_cos" || id == "3") {
    return LightFunctionProfile::AngleCos;
  }
  return LightFunctionProfile::Off;
}

float EvalLightFunctionFactor(LightFunctionProfile profile, float uv_or_angle) {
  switch (profile) {
    case LightFunctionProfile::Off:
      return 1.f;
    case LightFunctionProfile::SoftDisk: {
      const float t = std::clamp(uv_or_angle, 0.f, 1.f);
      // Smoothstep edge near 1.
      const float edge = 1.f - t;
      return std::clamp(edge * edge * (3.f - 2.f * edge), 0.f, 1.f);
    }
    case LightFunctionProfile::RadialFalloff: {
      const float t = std::clamp(uv_or_angle, 0.f, 1.f);
      return std::clamp(1.f - t * t, 0.f, 1.f);
    }
    case LightFunctionProfile::AngleCos: {
      const float c = std::clamp(uv_or_angle, -1.f, 1.f);
      return std::clamp(c * 0.5f + 0.5f, 0.f, 1.f);
    }
  }
  return 1.f;
}

}  // namespace engine::render
