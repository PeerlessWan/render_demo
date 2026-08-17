#pragma once

#include "engine/core/math.h"
#include "engine/render2d/atlas.h"
#include "engine/render2d/bmfont.h"
#include "engine/render2d/path2d.h"
#include "engine/render2d/rich_text.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render2d {

struct WorldTextVertex {
  Vec3 position{};
  float u = 0.f;
  float v = 0.f;
  ColorRgba color{1.f, 1.f, 1.f, 1.f};
};

struct WorldTextMesh {
  std::vector<WorldTextVertex> vertices;
  std::vector<std::uint32_t> indices;
  // Optional atlas page / texture hint for hosts pairing BMFont with GPU texture.
  std::string atlas_page;
  int atlas_w = 256;
  int atlas_h = 256;
};

// C14 / W7: billboard quads in world space facing camera (right/up from camera basis).
// atlas_w/h used to convert glyph pixel rects to UVs; if <=0 uses 256.
WorldTextMesh BuildWorldTextBillboards(const BmFontAtlas& atlas, std::string_view text,
                                       const Vec3& origin, const Vec3& cam_right,
                                       const Vec3& cam_up, float world_scale,
                                       int atlas_w = 256, int atlas_h = 256);

// C14 / Mega-W8: same as above but with per-span colors from ParseRichTextSpans.
WorldTextMesh BuildWorldTextBillboardsSpans(const BmFontAtlas& atlas,
                                            const std::vector<RichTextSpan>& spans,
                                            const Vec3& origin, const Vec3& cam_right,
                                            const Vec3& cam_up, float world_scale,
                                            int atlas_w = 256, int atlas_h = 256);

// Pair BMFont with a texture atlas frame (uses frame UV rect as page bounds).
// Glyph pixel rects are remapped into [frame.u0,frame.u1]×[frame.v0,frame.v1].
WorldTextMesh BuildWorldTextBillboardsWithAtlasFrame(const BmFontAtlas& atlas,
                                                     const AtlasFrame& frame,
                                                     std::string_view text, const Vec3& origin,
                                                     const Vec3& cam_right, const Vec3& cam_up,
                                                     float world_scale, int atlas_w = 256,
                                                     int atlas_h = 256);

// Textured 2D path stroke in world/XZ (Y up): Path2D points are XZ; V across stroke.
Path2DMesh BuildWorldTexturedPathStroke(const Path2D& path, float half_width);

}  // namespace engine::render2d
