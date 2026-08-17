#include "engine/render2d/world_text.h"

#include <cmath>

namespace engine::render2d {

WorldTextMesh BuildWorldTextBillboards(const BmFontAtlas& atlas, std::string_view text,
                                       const Vec3& origin, const Vec3& cam_right,
                                       const Vec3& cam_up, float world_scale, int atlas_w,
                                       int atlas_h) {
  WorldTextMesh mesh;
  if (text.empty() || world_scale <= 0.f) {
    return mesh;
  }
  const float aw = static_cast<float>(atlas_w > 0 ? atlas_w : 256);
  const float ah = static_cast<float>(atlas_h > 0 ? atlas_h : 256);
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

  float cursor = 0.f;
  const float lh = static_cast<float>(atlas.line_height > 0 ? atlas.line_height : 16);
  for (unsigned char ch : text) {
    if (ch == '\n') {
      cursor = 0.f;
      continue;
    }
    const auto it = atlas.glyphs.find(static_cast<char32_t>(ch));
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
    const float x0 = cursor;
    const float y0 = 0.f;
    const float x1 = cursor + gw;
    const float y1 = gh;
    const float u0 = static_cast<float>(g.x) / aw;
    const float v0 = static_cast<float>(g.y) / ah;
    const float u1 = static_cast<float>(g.x + g.w) / aw;
    const float v1 = static_cast<float>(g.y + g.h) / ah;

    const auto corner = [&](float x, float y) {
      return origin + right * x + up * (y - lh * world_scale * 0.5f);
    };
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({corner(x0, y0), u0, v1});
    mesh.vertices.push_back({corner(x1, y0), u1, v1});
    mesh.vertices.push_back({corner(x1, y1), u1, v0});
    mesh.vertices.push_back({corner(x0, y1), u0, v0});
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);

    cursor += static_cast<float>(g.xadvance > 0 ? g.xadvance : g.w) * world_scale;
  }
  return mesh;
}

}  // namespace engine::render2d
