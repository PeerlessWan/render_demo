#include "editing/tile_edit.h"

#include "engine/scene/world.h"

#include <algorithm>
#include <string_view>

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
                          std::vector<engine::render2d::Sprite>* out, std::string_view atlas) {
  if (!out) {
    return;
  }
  engine::render2d::TileExpandDesc desc;
  desc.tile_w = 1.f;
  desc.tile_h = 1.f;
  desc.atlas_id = std::string(atlas);
  streamer.ExpandResidentToSprites(*out, desc);
}

void CollectWorldSprites(const engine::scene::World& world, std::vector<engine::render2d::Sprite>* out) {
  if (!out) {
    return;
  }
  std::vector<engine::scene::NodeId> nodes;
  std::vector<engine::scene::NodeId> stack = world.roots();
  while (!stack.empty()) {
    const auto id = stack.back();
    stack.pop_back();
    if (!world.valid(id)) {
      continue;
    }
    for (auto c : world.children(id)) {
      stack.push_back(c);
    }
    const auto* spr = world.sprite(id);
    if (!spr) {
      continue;
    }
    const auto& m = world.world_matrix(id);
    engine::render2d::Sprite s;
    s.atlas_id = spr->atlas_id;
    s.frame = spr->gid;
    s.position = {m.m[12], m.m[14]};
    s.size = {1.f, 1.f};
    s.sort_layer = spr->sort_layer;
    s.sort_y = s.position.y;
    out->push_back(std::move(s));
  }
}

}  // namespace editor
