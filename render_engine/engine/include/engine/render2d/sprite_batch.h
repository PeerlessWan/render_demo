#pragma once

#include "engine/core/math.h"
#include "engine/render2d/atlas.h"
#include "engine/render2d/sprite.h"
#include "engine/rhi/i_device.h"

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::render2d {

struct AtlasBank {
  std::unordered_map<std::string, std::vector<AtlasFrame>> by_id;
};

// Resolve UV for sprite; missing atlas → full white quad UV.
bool ResolveSpriteUv(const AtlasBank& bank, const Sprite& spr, float* u0, float* v0, float* u1,
                     float* v1);

// Build textured quads in screen space (already Camera2D-projected by caller).
void BuildTexturedQuads(std::span<const Sprite> sprites, const AtlasBank& bank,
                        std::vector<rhi::TexturedQuad>* out);

}  // namespace engine::render2d
