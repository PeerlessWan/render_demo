#include "engine/render/atmosphere.h"

#include <algorithm>
#include <cmath>

namespace engine::render {
namespace {

float Saturate(float x) { return std::clamp(x, 0.f, 1.f); }

}  // namespace

ColorRgba EvalSkyColor(const AtmosphereParams& params, const Vec3& dir) {
  Vec3 v = Normalize(dir);
  if (v.length_squared() < 1e-12f) {
    v = Vec3{0.f, 1.f, 0.f};
  }
  Vec3 sun = Normalize(params.sun_dir);
  if (sun.length_squared() < 1e-12f) {
    sun = Vec3{0.3f, 1.f, 0.2f};
    sun = Normalize(sun);
  }
  // Prefer "up" as +Y; flip sun so intensity uses elevation above horizon.
  if (sun.y < 0.f) {
    sun = sun * -1.f;
  }

  const float turbidity = std::max(params.turbidity, 1.f);
  const float cos_theta = Saturate(v.y);  // 0 horizon → 1 zenith
  const float cos_gamma = Saturate(Dot(v, sun));
  const float gamma = std::acos(std::clamp(cos_gamma, -1.f, 1.f));

  // Phase-ish + optical-depth stand-in (not a full Preetham/Bruneton model).
  const float phase = 0.75f * (1.f + cos_gamma * cos_gamma);
  const float optical = std::exp(-0.15f * turbidity * (1.f - cos_theta));
  const float sun_disk = std::exp(-gamma * gamma * (28.f + turbidity * 4.f));

  const Vec3 rs = params.rayleigh_scale;
  const float inv_t = 1.f / turbidity;
  Vec3 sky;
  sky.x = (0.18f * inv_t + rs.x * 0.55f * optical * phase) + sun_disk * 1.8f;
  sky.y = (0.28f * inv_t + rs.y * 0.55f * optical * phase) + sun_disk * 1.4f;
  sky.z = (0.55f * inv_t + rs.z * 0.55f * optical * phase) + sun_disk * 0.9f;

  // Horizon warm haze.
  const float haze = (1.f - cos_theta) * (0.08f * turbidity);
  sky.x += haze * 0.35f;
  sky.y += haze * 0.22f;
  sky.z += haze * 0.10f;

  sky.x = std::max(sky.x, 0.f);
  sky.y = std::max(sky.y, 0.f);
  sky.z = std::max(sky.z, 0.f);
  if (!std::isfinite(sky.x) || !std::isfinite(sky.y) || !std::isfinite(sky.z)) {
    return ColorRgba{0.45f, 0.55f, 0.75f, 1.f};
  }
  return ColorRgba{sky.x, sky.y, sky.z, 1.f};
}

}  // namespace engine::render
