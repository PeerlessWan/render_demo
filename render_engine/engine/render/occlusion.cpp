#include "engine/render/occlusion.h"

#include <algorithm>
#include <cmath>

namespace engine::render {

void OcclusionBuffer::Configure(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  const int max_dim = std::max(width_, height_);
  pyramid_levels_ = static_cast<int>(std::floor(std::log2(static_cast<float>(max_dim)))) + 1;
}

bool OcclusionBuffer::IsVisible(const Aabb& box, const Mat4& view_proj) const {
  (void)pyramid_levels_;
  // TODO(soft-hiz): sample depth pyramid mips once GPU path exists.
  const Frustum frustum = Frustum::FromViewProj(view_proj);
  return frustum.ContainsAabb(box);
}

}  // namespace engine::render
