#pragma once

#include "engine/core/math.h"

namespace engine::render {

// M10: occlusion culling stub. Depth pyramid reserved for future soft HiZ path.
class OcclusionBuffer {
 public:
  void Configure(int width, int height);

  // Placeholder HiZ: frustum test only until GPU depth pyramid is wired.
  [[nodiscard]] bool IsVisible(const Aabb& box, const Mat4& view_proj) const;

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }

 private:
  int width_ = 0;
  int height_ = 0;
  // Soft HiZ placeholder: mip chain size reserved for future GPU upload.
  int pyramid_levels_ = 0;
};

}  // namespace engine::render
