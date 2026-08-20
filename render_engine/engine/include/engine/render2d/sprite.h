#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
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
  ColorRgba color{0.2f, 0.85f, 0.35f, 0.9f};
  // W21: Godot CanvasItem-lite
  ColorRgba modulate{1.f, 1.f, 1.f, 1.f};
  std::string normal_tex;  // optional normal map id (CPU Light2D uses has_normals)
  bool has_normals = false;
  std::uint32_t layer_mask = 0xFFFFFFFFu;
};

struct TilemapLayer {
  std::string name;
  std::string type;  // Tiled layer type (e.g. "tilelayer")
  int width = 0;
  int height = 0;
  int tile_w = 16;
  int tile_h = 16;
  std::vector<int> gids;
  std::string tileset_image;  // bound from tilesets[].image when present
  bool collision = false;     // name contains "collision" or type hints collision
};

void SortSprites(std::vector<Sprite>& sprites);

// Parse Tiled map JSON (orthogonal tile layers). Multiple layers supported.
// Collision layers (name contains "collision", case-insensitive) are flagged.
Status LoadTiledJson(const std::filesystem::path& path, std::vector<TilemapLayer>& out_layers);

// Export first collision layer's gid grid (row-major). Returns false if none.
bool ExportCollisionGids(const std::vector<TilemapLayer>& layers, std::vector<int>& out_gids,
                         int& out_width, int& out_height);

}  // namespace engine::render2d
