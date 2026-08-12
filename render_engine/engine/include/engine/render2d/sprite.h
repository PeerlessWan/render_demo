#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace engine::render2d {

struct Sprite {
  std::string atlas_id;
  int frame = 0;
  Vec2 position{};
  Vec2 size{16, 16};
  int sort_layer = 0;
  float sort_y = 0.f;
  bool nearest = true;
};

struct TilemapLayer {
  std::string name;
  int width = 0;
  int height = 0;
  int tile_w = 16;
  int tile_h = 16;
  std::vector<int> gids;
};

void SortSprites(std::vector<Sprite>& sprites);
Status LoadTiledJson(const std::filesystem::path& path, std::vector<TilemapLayer>& out_layers);

}  // namespace engine::render2d
