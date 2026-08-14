#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::render {

// Soft HiZ occlusion: frustum + optional CPU depth pyramid (max-filter mips).
class OcclusionBuffer {
 public:
  void Configure(int width, int height);

  // Upload finest mip (width*height floats in [0,1], larger = farther). Builds pyramid.
  void UploadDepthFinest(std::span<const float> depth_hw);

  // Clear HiZ; IsVisible falls back to frustum only.
  void ClearHiZ();

  [[nodiscard]] bool IsVisible(const Aabb& box, const Mat4& view_proj) const;

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] int pyramid_levels() const { return pyramid_levels_; }
  [[nodiscard]] bool has_hiz() const { return !pyramid_.empty(); }

 private:
  [[nodiscard]] float SampleMaxDepth(float u0, float v0, float u1, float v1) const;

  int width_ = 0;
  int height_ = 0;
  int pyramid_levels_ = 0;
  std::vector<float> pyramid_;
  std::vector<int> level_w_;
  std::vector<int> level_h_;
  std::vector<std::size_t> level_offset_;
};

}  // namespace engine::render
