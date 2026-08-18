#pragma once

#include "engine/render2d/sprite.h"
#include "engine/render2d/tilemap_stream.h"

#include <vector>

namespace editor {

inline constexpr int kTileMap = 16;

void EnsureTiles(std::vector<int>* tiles);

void PaintTile(std::vector<int>* tiles, engine::render2d::TilemapStreamer* streamer, int x, int y,
               int gid);

void SyncStreamer(const std::vector<int>& tiles, engine::render2d::TilemapStreamer* streamer);

void ExpandTilesToSprites(const engine::render2d::TilemapStreamer& streamer,
                          std::vector<engine::render2d::Sprite>* out);

}  // namespace editor
