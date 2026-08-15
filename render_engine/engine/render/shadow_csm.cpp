#include "engine/render/shadow_csm.h"

#include <algorithm>
#include <cmath>

namespace engine::render {

void CascadedShadowMap::set_cascade_count(int n) {
  count_ = n < 1 ? 1 : (n > 4 ? 4 : n);
}

std::vector<float> CascadedShadowMap::ComputeSplits(float z_near, float z_far, int count,
                                                    float lambda) {
  std::vector<float> splits;
  splits.reserve(static_cast<std::size_t>(count));
  for (int i = 1; i <= count; ++i) {
    const float p = static_cast<float>(i) / static_cast<float>(count);
    const float log_s = z_near * std::pow(z_far / z_near, p);
    const float uni_s = z_near + (z_far - z_near) * p;
    splits.push_back(lambda * log_s + (1.f - lambda) * uni_s);
  }
  return splits;
}

void CascadedShadowMap::FrustumSliceCorners(const Camera& camera, float aspect, float near_z,
                                            float far_z, std::array<Vec3, 8>& out_corners) {
  const float a = aspect <= 0.f ? 1.f : aspect;
  const float tan_half = std::tan(camera.fovy_rad * 0.5f);
  const float nh = tan_half * near_z;
  const float nw = nh * a;
  const float fh = tan_half * far_z;
  const float fw = fh * a;

  const Quat q = Quat::FromEulerYxz(camera.yaw, camera.pitch, 0.f);
  const Vec3 forward = q.Rotate(Vec3{0.f, 0.f, -1.f});
  const Vec3 right = q.Rotate(Vec3{1.f, 0.f, 0.f});
  const Vec3 up = Normalize(Cross(right, forward));

  const Vec3 nc = camera.position + forward * near_z;
  const Vec3 fc = camera.position + forward * far_z;

  out_corners[0] = nc + up * nh - right * nw;
  out_corners[1] = nc + up * nh + right * nw;
  out_corners[2] = nc - up * nh - right * nw;
  out_corners[3] = nc - up * nh + right * nw;
  out_corners[4] = fc + up * fh - right * fw;
  out_corners[5] = fc + up * fh + right * fw;
  out_corners[6] = fc - up * fh - right * fw;
  out_corners[7] = fc - up * fh + right * fw;
}

Mat4 CascadedShadowMap::FitLightMatrix(const Vec3& light_dir, const std::array<Vec3, 8>& corners,
                                       int map_resolution) {
  Vec3 center{};
  for (const auto& c : corners) {
    center += c;
  }
  center = center * (1.f / 8.f);

  const Vec3 dir = Normalize(light_dir);
  // Stable up: avoid parallel with light.
  Vec3 up{0.f, 1.f, 0.f};
  if (std::fabs(Dot(dir, up)) > 0.95f) {
    up = Vec3{0.f, 0.f, 1.f};
  }

  float radius = 0.f;
  for (const auto& c : corners) {
    radius = std::max(radius, (c - center).length());
  }
  // Fixed sphere extent stops XY bounds from crawling when the camera turns.
  radius = std::max(radius + 1.f, 8.f);
  const float extent = radius;

  // Pull the light back along -dir; depth range covers casters behind the slice.
  const float z_pull = extent + 20.f;
  Vec3 eye = center - dir * z_pull;
  Mat4 light_view = Mat4::LookAt(eye, center, up);

  // Texel-snap the sphere center in light space so the shadow map doesn't shimmer
  // when the camera (and thus the cascade sphere) moves.
  const float res = static_cast<float>(std::max(map_resolution, 1));
  const float units_per_texel = (extent * 2.f) / res;
  if (units_per_texel > 1e-6f) {
    const Vec3 center_ls = light_view.TransformPoint(center);
    const float snap_x = std::floor(center_ls.x / units_per_texel + 0.5f) * units_per_texel;
    const float snap_y = std::floor(center_ls.y / units_per_texel + 0.5f) * units_per_texel;
    const float dx = snap_x - center_ls.x;
    const float dy = snap_y - center_ls.y;
    // Match Mat4::LookAt basis: s=Cross(f,up), u=Cross(s,f), f=dir.
    const Vec3 s = Normalize(Cross(dir, up));
    const Vec3 u = Cross(s, dir);
    center = center + s * dx + u * dy;
    eye = center - dir * z_pull;
    light_view = Mat4::LookAt(eye, center, up);
  }

  // Depth: cover casters beyond the sphere (stable Z range in light space).
  float z_min = 1e9f;
  float z_max = -1e9f;
  for (const auto& c : corners) {
    const Vec3 lp = light_view.TransformPoint(c);
    z_min = std::min(z_min, lp.z);
    z_max = std::max(z_max, lp.z);
  }
  z_min -= extent * 1.0f + 10.f;
  z_max += extent * 0.5f + 4.f;

  const Mat4 light_proj =
      Mat4::Orthographic(-extent, extent, -extent, extent, z_min, z_max);
  return light_proj * light_view;
}

void CascadedShadowMap::Build(const Camera& camera, float aspect, const Vec3& light_dir,
                              int atlas_size, float max_shadow_distance) {
  cascades_.clear();
  const float z_near = std::max(camera.z_near, 0.05f);
  const float z_far = std::min(camera.z_far, max_shadow_distance);
  tiles_per_row_ = count_ <= 1 ? 1 : 2;
  const int tile = atlas_size / tiles_per_row_;
  const auto splits = ComputeSplits(z_near, z_far, count_);

  float prev = z_near;
  for (int i = 0; i < count_; ++i) {
    CsmCascade c;
    c.split = splits[static_cast<std::size_t>(i)];
    std::array<Vec3, 8> corners{};
    FrustumSliceCorners(camera, aspect, prev, c.split, corners);
    c.view_proj = FitLightMatrix(light_dir, corners, tile);
    c.atlas_x = (i % tiles_per_row_) * tile;
    c.atlas_y = (i / tiles_per_row_) * tile;
    c.atlas_w = tile;
    c.atlas_h = tile;
    cascades_.push_back(c);
    prev = c.split;
  }
}

}  // namespace engine::render
