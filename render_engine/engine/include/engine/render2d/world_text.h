#pragma once

#include "engine/core/math.h"
#include "engine/render2d/bmfont.h"

#include <string_view>
#include <vector>

namespace engine::render2d {

struct WorldTextVertex {
  Vec3 position{};
  float u = 0.f;
  float v = 0.f;
};

struct WorldTextMesh {
  std::vector<WorldTextVertex> vertices;
  std::vector<std::uint32_t> indices;
};

// C14 / W7: billboard quads in world space facing camera (right/up from camera basis).
// atlas_w/h used to convert glyph pixel rects to UVs; if <=0 uses 256.
WorldTextMesh BuildWorldTextBillboards(const BmFontAtlas& atlas, std::string_view text,
                                       const Vec3& origin, const Vec3& cam_right,
                                       const Vec3& cam_up, float world_scale,
                                       int atlas_w = 256, int atlas_h = 256);

}  // namespace engine::render2d
