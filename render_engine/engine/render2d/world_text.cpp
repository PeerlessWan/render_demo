#include "engine/render2d/world_text.h"

#include <cmath>

namespace engine::render2d {
namespace {

WorldTextMesh BuildSpansImpl(const BmFontAtlas& atlas, const std::vector<RichTextSpan>& spans,
                             const Vec3& origin, const Vec3& cam_right, const Vec3& cam_up,
                             float world_scale, int atlas_w, int atlas_h, float u_scale,
                             float v_scale, float u_bias, float v_bias) {
  WorldTextMesh mesh;
  mesh.atlas_w = atlas_w > 0 ? atlas_w : 256;
  mesh.atlas_h = atlas_h > 0 ? atlas_h : 256;
  if (!atlas.pages.empty()) {
    mesh.atlas_page = atlas.pages.front();
  }
  if (spans.empty() || world_scale <= 0.f) {
    return mesh;
  }
  const float aw = static_cast<float>(mesh.atlas_w);
  const float ah = static_cast<float>(mesh.atlas_h);
  Vec3 right = cam_right;
  Vec3 up = cam_up;
  if (right.length_squared() < 1e-8f) {
    right = Vec3{1, 0, 0};
  } else {
    right = Normalize(right);
  }
  if (up.length_squared() < 1e-8f) {
    up = Vec3{0, 1, 0};
  } else {
    up = Normalize(up);
  }

  float cursor_x = 0.f;
  float cursor_y = 0.f;
  const float lh = static_cast<float>(atlas.line_height > 0 ? atlas.line_height : 16);

  auto emit_glyph = [&](char32_t ch, const ColorRgba& color) {
    if (ch == '\n') {
      cursor_x = 0.f;
      cursor_y -= lh * world_scale;
      return;
    }
    const auto it = atlas.glyphs.find(ch);
    BmGlyph g;
    if (it != atlas.glyphs.end()) {
      g = it->second;
    } else {
      g.w = 8;
      g.h = 8;
      g.xadvance = 8;
    }
    const float gw = static_cast<float>(g.w) * world_scale;
    const float gh = static_cast<float>(g.h) * world_scale;
    const float x0 = cursor_x;
    const float y0 = cursor_y;
    const float x1 = cursor_x + gw;
    const float y1 = cursor_y + gh;
    const float u0 = u_bias + (static_cast<float>(g.x) / aw) * u_scale;
    const float v0 = v_bias + (static_cast<float>(g.y) / ah) * v_scale;
    const float u1 = u_bias + (static_cast<float>(g.x + g.w) / aw) * u_scale;
    const float v1 = v_bias + (static_cast<float>(g.y + g.h) / ah) * v_scale;

    const auto corner = [&](float x, float y) {
      return origin + right * x + up * (y - lh * world_scale * 0.5f);
    };
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({corner(x0, y0), u0, v1, color});
    mesh.vertices.push_back({corner(x1, y0), u1, v1, color});
    mesh.vertices.push_back({corner(x1, y1), u1, v0, color});
    mesh.vertices.push_back({corner(x0, y1), u0, v0, color});
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);

    cursor_x += static_cast<float>(g.xadvance > 0 ? g.xadvance : g.w) * world_scale;
  };

  for (const auto& span : spans) {
    for (unsigned char ch : span.text) {
      emit_glyph(static_cast<char32_t>(ch), span.color);
    }
  }
  return mesh;
}

}  // namespace

WorldTextMesh BuildWorldTextBillboards(const BmFontAtlas& atlas, std::string_view text,
                                       const Vec3& origin, const Vec3& cam_right,
                                       const Vec3& cam_up, float world_scale, int atlas_w,
                                       int atlas_h) {
  std::vector<RichTextSpan> spans;
  spans.push_back(RichTextSpan{std::string(text), ColorRgba{1.f, 1.f, 1.f, 1.f}});
  return BuildSpansImpl(atlas, spans, origin, cam_right, cam_up, world_scale, atlas_w, atlas_h, 1.f,
                        1.f, 0.f, 0.f);
}

WorldTextMesh BuildWorldTextBillboardsSpans(const BmFontAtlas& atlas,
                                            const std::vector<RichTextSpan>& spans,
                                            const Vec3& origin, const Vec3& cam_right,
                                            const Vec3& cam_up, float world_scale, int atlas_w,
                                            int atlas_h) {
  return BuildSpansImpl(atlas, spans, origin, cam_right, cam_up, world_scale, atlas_w, atlas_h, 1.f,
                        1.f, 0.f, 0.f);
}

WorldTextMesh BuildWorldTextBillboardsWithAtlasFrame(const BmFontAtlas& atlas,
                                                     const AtlasFrame& frame,
                                                     std::string_view text, const Vec3& origin,
                                                     const Vec3& cam_right, const Vec3& cam_up,
                                                     float world_scale, int atlas_w,
                                                     int atlas_h) {
  const float u_scale = frame.u1 - frame.u0;
  const float v_scale = frame.v1 - frame.v0;
  std::vector<RichTextSpan> spans;
  spans.push_back(RichTextSpan{std::string(text), ColorRgba{1.f, 1.f, 1.f, 1.f}});
  auto mesh = BuildSpansImpl(atlas, spans, origin, cam_right, cam_up, world_scale, atlas_w, atlas_h,
                             u_scale, v_scale, frame.u0, frame.v0);
  mesh.atlas_page = frame.name;
  return mesh;
}

Path2DMesh BuildWorldTexturedPathStroke(const Path2D& path, float half_width) {
  return path.BuildTexturedStrokeTriangleStrip(half_width);
}

}  // namespace engine::render2d
