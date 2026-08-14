#include "engine/render/occlusion.h"

#include "engine/core/feature.h"

#include <algorithm>
#include <cmath>

namespace engine::render {
namespace {

float Clampf(float x, float a, float b) { return std::min(b, std::max(a, x)); }

}  // namespace

void OcclusionBuffer::Configure(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  const int max_dim = std::max(width_, height_);
  pyramid_levels_ = static_cast<int>(std::floor(std::log2(static_cast<float>(max_dim)))) + 1;
  ClearHiZ();
}

void OcclusionBuffer::ClearHiZ() {
  pyramid_.clear();
  level_w_.clear();
  level_h_.clear();
  level_offset_.clear();
}

void OcclusionBuffer::UploadDepthFinest(std::span<const float> depth_hw) {
  const std::size_t expected = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
  if (depth_hw.size() < expected || width_ <= 0 || height_ <= 0) {
    ClearHiZ();
    return;
  }
  level_w_.assign(static_cast<std::size_t>(pyramid_levels_), 0);
  level_h_.assign(static_cast<std::size_t>(pyramid_levels_), 0);
  level_offset_.assign(static_cast<std::size_t>(pyramid_levels_), 0);
  std::size_t total = 0;
  int w = width_;
  int h = height_;
  for (int l = 0; l < pyramid_levels_; ++l) {
    level_w_[static_cast<std::size_t>(l)] = w;
    level_h_[static_cast<std::size_t>(l)] = h;
    level_offset_[static_cast<std::size_t>(l)] = total;
    total += static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    w = std::max(1, w / 2);
    h = std::max(1, h / 2);
  }
  pyramid_.assign(total, 1.f);
  std::copy_n(depth_hw.begin(), expected, pyramid_.begin());
  for (int l = 1; l < pyramid_levels_; ++l) {
    const int pw = level_w_[static_cast<std::size_t>(l - 1)];
    const int ph = level_h_[static_cast<std::size_t>(l - 1)];
    const int cw = level_w_[static_cast<std::size_t>(l)];
    const int ch = level_h_[static_cast<std::size_t>(l)];
    const std::size_t po = level_offset_[static_cast<std::size_t>(l - 1)];
    const std::size_t co = level_offset_[static_cast<std::size_t>(l)];
    for (int y = 0; y < ch; ++y) {
      for (int x = 0; x < cw; ++x) {
        const int x0 = x * 2;
        const int y0 = y * 2;
        float m = 0.f;
        for (int dy = 0; dy < 2; ++dy) {
          for (int dx = 0; dx < 2; ++dx) {
            const int sx = std::min(pw - 1, x0 + dx);
            const int sy = std::min(ph - 1, y0 + dy);
            m = std::max(m, pyramid_[po + static_cast<std::size_t>(sy * pw + sx)]);
          }
        }
        pyramid_[co + static_cast<std::size_t>(y * cw + x)] = m;
      }
    }
  }
  engine::SetFeatureOverride("hiz", true);
}

float OcclusionBuffer::SampleMaxDepth(float u0, float v0, float u1, float v1) const {
  if (pyramid_.empty()) {
    return 1.f;
  }
  u0 = Clampf(u0, 0.f, 1.f);
  v0 = Clampf(v0, 0.f, 1.f);
  u1 = Clampf(u1, 0.f, 1.f);
  v1 = Clampf(v1, 0.f, 1.f);
  if (u1 < u0) {
    std::swap(u0, u1);
  }
  if (v1 < v0) {
    std::swap(v0, v1);
  }
  const float du = std::max(u1 - u0, 1.f / static_cast<float>(width_));
  const float dv = std::max(v1 - v0, 1.f / static_cast<float>(height_));
  const float span = std::max(du, dv);
  int level = static_cast<int>(std::floor(std::log2(span * static_cast<float>(width_))));
  level = std::clamp(level, 0, pyramid_levels_ - 1);
  const int lw = level_w_[static_cast<std::size_t>(level)];
  const int lh = level_h_[static_cast<std::size_t>(level)];
  const std::size_t off = level_offset_[static_cast<std::size_t>(level)];
  const int x0 = std::clamp(static_cast<int>(u0 * static_cast<float>(lw)), 0, lw - 1);
  const int y0 = std::clamp(static_cast<int>(v0 * static_cast<float>(lh)), 0, lh - 1);
  const int x1 = std::clamp(static_cast<int>(u1 * static_cast<float>(lw)), 0, lw - 1);
  const int y1 = std::clamp(static_cast<int>(v1 * static_cast<float>(lh)), 0, lh - 1);
  float m = 0.f;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      m = std::max(m, pyramid_[off + static_cast<std::size_t>(y * lw + x)]);
    }
  }
  return m;
}

bool OcclusionBuffer::IsVisible(const Aabb& box, const Mat4& view_proj) const {
  const Frustum frustum = Frustum::FromViewProj(view_proj);
  if (!frustum.ContainsAabb(box)) {
    return false;
  }
  if (pyramid_.empty()) {
    return true;
  }
  // Project 8 corners to NDC, take screen AABB and nearest depth.
  float u0 = 1.f, v0 = 1.f, u1 = 0.f, v1 = 0.f;
  float nearest_z = 1.f;
  bool any = false;
  const Vec3 corners[8] = {
      {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
      {box.min.x, box.max.y, box.min.z}, {box.max.x, box.max.y, box.min.z},
      {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
      {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z},
  };
  for (const auto& c : corners) {
    const Vec3 ndc = view_proj.TransformPoint(c);
    const float u = ndc.x * 0.5f + 0.5f;
    const float v = ndc.y * -0.5f + 0.5f;
    u0 = std::min(u0, u);
    v0 = std::min(v0, v);
    u1 = std::max(u1, u);
    v1 = std::max(v1, v);
    nearest_z = std::min(nearest_z, ndc.z);
    any = true;
  }
  if (!any) {
    return true;
  }
  const float occluder_z = SampleMaxDepth(u0, v0, u1, v1);
  // Conservative: visible if nearest point is in front of occluder max depth.
  return nearest_z <= occluder_z + 0.002f;
}

}  // namespace engine::render
