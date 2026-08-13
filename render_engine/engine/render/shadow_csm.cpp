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
  const float radius =
      std::max({(corners[0] - center).length(), (corners[1] - center).length(),
                (corners[2] - center).length(), (corners[3] - center).length(),
                (corners[4] - center).length(), (corners[5] - center).length(),
                (corners[6] - center).length(), (corners[7] - center).length()}) +
      1.f;
  const Vec3 eye = center - dir * (radius + 20.f);
  const Mat4 light_view = Mat4::LookAt(eye, center, up);

  Vec3 bmin{1e9f, 1e9f, 1e9f};
  Vec3 bmax{-1e9f, -1e9f, -1e9f};
  for (const auto& c : corners) {
    const Vec3 lp = light_view.TransformPoint(c);
    bmin.x = std::min(bmin.x, lp.x);
    bmin.y = std::min(bmin.y, lp.y);
    bmin.z = std::min(bmin.z, lp.z);
    bmax.x = std::max(bmax.x, lp.x);
    bmax.y = std::max(bmax.y, lp.y);
    bmax.z = std::max(bmax.z, lp.z);
  }

  // Expand Z so casters behind the slice still contribute.
  bmin.z -= radius * 0.5f;
  bmax.z += 2.f;

  // Texel snap to reduce shimmer.
  const float res = static_cast<float>(std::max(map_resolution, 1));
  const float world_units_per_texel_x = (bmax.x - bmin.x) / res;
  const float world_units_per_texel_y = (bmax.y - bmin.y) / res;
  if (world_units_per_texel_x > 1e-6f) {
    bmin.x = std::floor(bmin.x / world_units_per_texel_x) * world_units_per_texel_x;
    bmax.x = std::floor(bmax.x / world_units_per_texel_x) * world_units_per_texel_x;
  }
  if (world_units_per_texel_y > 1e-6f) {
    bmin.y = std::floor(bmin.y / world_units_per_texel_y) * world_units_per_texel_y;
    bmax.y = std::floor(bmax.y / world_units_per_texel_y) * world_units_per_texel_y;
  }

  const Mat4 light_proj =
      Mat4::Orthographic(bmin.x, bmax.x, bmin.y, bmax.y, bmin.z, bmax.z);
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
