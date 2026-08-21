#include "engine/render2d/tile_draw.h"

#include <algorithm>

namespace engine::render2d {

void ExpandTileLayerToSprites(const TilemapLayer& layer, const std::string& atlas_id,
                              const std::vector<AtlasFrame>& frames, int z_index,
                              std::vector<Sprite>* out) {
  if (!out || layer.width <= 0 || layer.height <= 0) {
    return;
  }
  const int n = layer.width * layer.height;
  if (static_cast<int>(layer.gids.size()) < n) {
    return;
  }
  for (int y = 0; y < layer.height; ++y) {
    for (int x = 0; x < layer.width; ++x) {
      const int gid = layer.gids[static_cast<std::size_t>(y * layer.width + x)];
      if (gid <= 0) {
        continue;
      }
      Sprite spr;
      spr.atlas_id = atlas_id;
      spr.frame = gid - 1;
      if (!frames.empty()) {
        spr.frame = std::clamp(spr.frame, 0, static_cast<int>(frames.size()) - 1);
      }
      spr.position = {static_cast<float>(x * layer.tile_w), static_cast<float>(y * layer.tile_h)};
      spr.size = {static_cast<float>(layer.tile_w), static_cast<float>(layer.tile_h)};
      spr.sort_layer = z_index;
      spr.sort_y = spr.position.y;
      spr.color = {1.f, 1.f, 1.f, 1.f};
      out->push_back(std::move(spr));
    }
  }
}

void ExpandTilemapToSprites(const std::vector<TilemapLayer>& layers, const AtlasBank& bank,
                            const std::string& default_atlas_id, std::vector<Sprite>* out) {
  if (!out) {
    return;
  }
  out->clear();
  int z = 0;
  for (const auto& layer : layers) {
    if (layer.collision) {
      continue;
    }
    std::string atlas = default_atlas_id;
    if (!layer.tileset_image.empty() && bank.by_id.count(layer.tileset_image)) {
      atlas = layer.tileset_image;
    }
    std::vector<AtlasFrame> frames;
    auto it = bank.by_id.find(atlas);
    if (it != bank.by_id.end()) {
      frames = it->second;
    }
    ExpandTileLayerToSprites(layer, atlas, frames, z++, out);
  }
  SortSprites(*out);
}

}  // namespace engine::render2d
