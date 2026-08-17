#include "engine/render/atmosphere.h"

#include <algorithm>
#include <cmath>

namespace engine::render {
namespace {

float Saturate(float x) { return std::clamp(x, 0.f, 1.f); }

float Hash31(float x, float y, float z) {
  const float n = std::sin(x * 127.1f + y * 311.7f + z * 74.7f) * 43758.5453f;
  return n - std::floor(n);
}

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
  if (sun.y < 0.f) {
    sun = sun * -1.f;
  }

  const float turbidity = std::max(params.turbidity, 1.f);
  const float cos_theta = Saturate(v.y);
  const float cos_gamma = Saturate(Dot(v, sun));
  const float gamma = std::acos(std::clamp(cos_gamma, -1.f, 1.f));

  const float phase = 0.75f * (1.f + cos_gamma * cos_gamma);
  const float optical = std::exp(-0.15f * turbidity * (1.f - cos_theta));
  const float sun_disk = std::exp(-gamma * gamma * (28.f + turbidity * 4.f));

  const Vec3 rs = params.rayleigh_scale;
  const float inv_t = 1.f / turbidity;
  Vec3 sky;
  sky.x = (0.18f * inv_t + rs.x * 0.55f * optical * phase) + sun_disk * 1.8f;
  sky.y = (0.28f * inv_t + rs.y * 0.55f * optical * phase) + sun_disk * 1.4f;
  sky.z = (0.55f * inv_t + rs.z * 0.55f * optical * phase) + sun_disk * 0.9f;

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

float EvalCloudBand(const AtmosphereParams& params, const Vec3& dir) {
  Vec3 v = Normalize(dir);
  if (v.length_squared() < 1e-12f) {
    return 0.f;
  }
  const float elev = std::fabs(v.y);
  const float band = 1.f - Saturate(std::fabs(elev - params.cloud_elevation) /
                                    std::max(params.cloud_thickness, 1e-3f));
  // Cheap 3-tap noise along view azimuth.
  const float az = std::atan2(v.z, v.x);
  float n = 0.f;
  n += Hash31(az * 3.1f, elev * 5.7f, 0.2f) * 0.55f;
  n += Hash31(az * 7.3f, elev * 11.f, 1.4f) * 0.30f;
  n += Hash31(az * 13.f, elev * 2.2f, 2.8f) * 0.15f;
  const float dens = Saturate((n - (1.f - params.cloud_coverage)) / std::max(params.cloud_coverage, 1e-3f));
  return Saturate(band * dens);
}

ColorRgba EvalSkyWithClouds(const AtmosphereParams& params, const Vec3& dir) {
  const ColorRgba sky = EvalSkyColor(params, dir);
  const float c = EvalCloudBand(params, dir);
  if (c <= 1e-4f) {
    return sky;
  }
  const float grey = 0.55f + 0.35f * c;
  return ColorRgba{sky.r * (1.f - c) + grey * c, sky.g * (1.f - c) + grey * c,
                   sky.b * (1.f - c) + grey * c * 0.98f, 1.f};
}

CoupledFog CoupleFogWithAtmosphere(const AtmosphereParams& params, const Vec3& view_dir,
                                   float base_fog_density, bool enable_clouds) {
  CoupledFog out;
  const ColorRgba sky =
      enable_clouds ? EvalSkyWithClouds(params, view_dir) : EvalSkyColor(params, view_dir);
  out.fog_color = sky;
  const float cloud = enable_clouds ? EvalCloudBand(params, view_dir) : 0.f;
  out.fog_density = std::max(0.f, base_fog_density) * (1.f + 0.65f * cloud + 0.15f * params.turbidity);
  out.clear_color = {sky.r * 0.22f, sky.g * 0.25f, sky.b * 0.32f, 1.f};
  return out;
}

}  // namespace engine::render
