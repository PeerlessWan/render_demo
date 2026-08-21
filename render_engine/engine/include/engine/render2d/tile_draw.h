#pragma once

#include "engine/render2d/atlas.h"
#include "engine/render2d/sprite.h"
#include "engine/render2d/sprite_batch.h"

#include <vector>

namespace engine::render2d {

// Expand a TilemapLayer into sprites using atlas frames indexed by (gid-1).
// Empty/zero gids skipped. Positions in tile world units (tile_w/tile_h).
void ExpandTileLayerToSprites(const TilemapLayer& layer, const std::string& atlas_id,
                              const std::vector<AtlasFrame>& frames, int z_index,
                              std::vector<Sprite>* out);

// Convenience: expand all non-collision tile layers into one sprite list.
void ExpandTilemapToSprites(const std::vector<TilemapLayer>& layers, const AtlasBank& bank,
                            const std::string& default_atlas_id, std::vector<Sprite>* out);

}  // namespace engine::render2d
