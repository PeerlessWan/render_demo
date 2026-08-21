#include "engine/render2d/sprite_batch.h"

#include <algorithm>

namespace engine::render2d {

bool ResolveSpriteUv(const AtlasBank& bank, const Sprite& spr, float* u0, float* v0, float* u1,
                     float* v1) {
  if (!u0 || !v0 || !u1 || !v1) {
    return false;
  }
  *u0 = 0.f;
  *v0 = 0.f;
  *u1 = 1.f;
  *v1 = 1.f;
  if (spr.atlas_id.empty()) {
    return false;
  }
  auto it = bank.by_id.find(spr.atlas_id);
  if (it == bank.by_id.end() || it->second.empty()) {
    return false;
  }
  const int fi = std::clamp(spr.frame, 0, static_cast<int>(it->second.size()) - 1);
  const auto& fr = it->second[static_cast<std::size_t>(fi)];
  *u0 = fr.u0;
  *v0 = fr.v0;
  *u1 = fr.u1;
  *v1 = fr.v1;
  return true;
}

void BuildTexturedQuads(std::span<const Sprite> sprites, const AtlasBank& bank,
                        std::vector<rhi::TexturedQuad>* out) {
  if (!out) {
    return;
  }
  out->clear();
  out->reserve(sprites.size());
  for (const auto& spr : sprites) {
    rhi::TexturedQuad q;
    q.x0 = spr.position.x;
    q.y0 = spr.position.y;
    q.x1 = spr.position.x + spr.size.x;
    q.y1 = spr.position.y + spr.size.y;
    ResolveSpriteUv(bank, spr, &q.u0, &q.v0, &q.u1, &q.v1);
    q.color = {spr.color.r * spr.modulate.r, spr.color.g * spr.modulate.g,
               spr.color.b * spr.modulate.b, spr.color.a * spr.modulate.a};
    out->push_back(q);
  }
}

}  // namespace engine::render2d
