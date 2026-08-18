#include "editing/tile_edit.h"

#include <algorithm>

namespace editor {

void EnsureTiles(std::vector<int>* tiles) {
  if (!tiles) {
    return;
  }
  if (tiles->size() != static_cast<std::size_t>(kTileMap * kTileMap)) {
    tiles->assign(static_cast<std::size_t>(kTileMap * kTileMap), 0);
  }
}

void PaintTile(std::vector<int>* tiles, engine::render2d::TilemapStreamer* streamer, int x, int y,
               int gid) {
  EnsureTiles(tiles);
  x = std::clamp(x, 0, kTileMap - 1);
  y = std::clamp(y, 0, kTileMap - 1);
  (*tiles)[static_cast<std::size_t>(y * kTileMap + x)] = gid;
  if (streamer) {
    streamer->SetGid(x, y, gid);
  }
}

void SyncStreamer(const std::vector<int>& tiles, engine::render2d::TilemapStreamer* streamer) {
  if (!streamer) {
    return;
  }
  streamer->Configure(kTileMap, kTileMap, 8, 8);
  const int n = std::min(static_cast<int>(tiles.size()), kTileMap * kTileMap);
  for (int i = 0; i < n; ++i) {
    streamer->SetGid(i % kTileMap, i / kTileMap, tiles[static_cast<std::size_t>(i)]);
  }
  streamer->UpdateResidence(8, 8, 4);
}

void ExpandTilesToSprites(const engine::render2d::TilemapStreamer& streamer,
                          std::vector<engine::render2d::Sprite>* out) {
  if (!out) {
    return;
  }
  engine::render2d::TileExpandDesc desc;
  desc.tile_w = 1.f;
  desc.tile_h = 1.f;
  streamer.ExpandResidentToSprites(*out, desc);
}

}  // namespace editor
