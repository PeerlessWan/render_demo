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
  // Bias lambda toward logarithmic near splits so cascade 0 keeps texel density on pillars.
  const float lam = std::clamp(lambda, 0.f, 1.f);
  for (int i = 1; i <= count; ++i) {
    const float p = static_cast<float>(i) / static_cast<float>(count);
    const float log_s = z_near * std::pow(z_far / z_near, p);
    const float uni_s = z_near + (z_far - z_near) * p;
    splits.push_back(lam * log_s + (1.f - lam) * uni_s);
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
  Vec3 up{0.f, 1.f, 0.f};
  if (std::fabs(Dot(dir, up)) > 0.95f) {
    up = Vec3{0.f, 0.f, 1.f};
  }

  float radius = 0.f;
  for (const auto& c : corners) {
    radius = std::max(radius, (c - center).length());
  }
  radius = std::max(radius + 1.f, 8.f);
  // World-unit quantum — avoids res-coupled near-identity ceil that fails to stabilize.
  constexpr float kRadiusQuantum = 0.5f;
  radius = std::ceil(radius / kRadiusQuantum) * kRadiusQuantum;
  const float extent = radius;

  const float z_pull = extent + 20.f;
  const Vec3 eye = center - dir * z_pull;
  Mat4 light_view = Mat4::LookAt(eye, center, up);

  const float res = static_cast<float>(std::max(map_resolution, 1));
  const float units_per_texel = (extent * 2.f) / res;
  if (units_per_texel > 1e-6f) {
    const Vec3 origin_ls = light_view.TransformPoint(Vec3{0.f, 0.f, 0.f});
    const float dx =
        std::floor(origin_ls.x / units_per_texel + 0.5f) * units_per_texel - origin_ls.x;
    const float dy =
        std::floor(origin_ls.y / units_per_texel + 0.5f) * units_per_texel - origin_ls.y;
    light_view = Mat4::Translation(Vec3{dx, dy, 0.f}) * light_view;
  }

  const float z_near = -z_pull - extent - 20.f;
  const float z_far = -z_pull + extent + 10.f;
  return Mat4::Orthographic(-extent, extent, -extent, extent, z_near, z_far) * light_view;
}

void CascadedShadowMap::Build(const Camera& camera, float aspect, const Vec3& light_dir,
                              int atlas_size, float max_shadow_distance) {
  cascades_.clear();
  const float z_near = std::max(camera.z_near, 0.05f);
  const float z_far = std::min(camera.z_far, max_shadow_distance);
  tiles_per_row_ = count_ <= 1 ? 1 : 2;
  const int tile = atlas_size / tiles_per_row_;
  // Stronger log bias → denser near cascade (pillar contact shadows).
  const auto splits = ComputeSplits(z_near, z_far, count_, 0.75f);

  float prev = z_near;
  for (int i = 0; i < count_; ++i) {
    CsmCascade c;
    c.split = splits[static_cast<std::size_t>(i)];
    const float span = c.split - prev;
    const float overlap = span * 0.2f;
    const float slice_near = (i == 0) ? prev : std::max(z_near, prev - overlap);
    const float slice_far = (i + 1 >= count_) ? c.split : (c.split + overlap);
    std::array<Vec3, 8> corners{};
    FrustumSliceCorners(camera, aspect, slice_near, slice_far, corners);
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
