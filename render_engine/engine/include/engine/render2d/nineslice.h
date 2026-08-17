#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <vector>

namespace engine::render2d {

// C13 / Mega-W8: 9-slice UV + geometry expand (shared by 2D UI / sprites).
struct NineSliceDesc {
  Vec2 position{};   // top-left of destination rect
  Vec2 size{64, 64}; // destination size in pixels
  // Source UV rect in atlas (full 9-slice image).
  float u0 = 0.f;
  float v0 = 0.f;
  float u1 = 1.f;
  float v1 = 1.f;
  // Border insets in source pixels (relative to source pixel size).
  float border_l = 8.f;
  float border_t = 8.f;
  float border_r = 8.f;
  float border_b = 8.f;
  // Source image pixel size used to convert border_px → UV fractions.
  float source_w = 64.f;
  float source_h = 64.f;
};

struct NineSliceVertex {
  Vec2 position{};
  float u = 0.f;
  float v = 0.f;
};

struct NineSliceMesh {
  std::vector<NineSliceVertex> vertices;
  std::vector<std::uint32_t> indices;  // triangle list
};

// Emits up to 9 quads (corners fixed, edges stretch 1-axis, center stretch 2-axis).
[[nodiscard]] NineSliceMesh ExpandNineSlice(const NineSliceDesc& desc);

}  // namespace engine::render2d
