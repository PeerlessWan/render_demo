#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::render2d {

struct AtlasFrame {
  int x = 0, y = 0, w = 16, h = 16;
};

struct TextureAtlas {
  std::string id;
  int width = 0;
  int height = 0;
  std::unordered_map<std::string, AtlasFrame> frames;
};

struct Bone2D {
  std::string name;
  int parent = -1;
  Vec2 bind_pos{};
  float bind_rot = 0.f;
};

struct Skeleton2D {
  std::vector<Bone2D> bones;
};

struct BonePose2D {
  std::vector<Vec2> positions;
  std::vector<float> rotations;
};

// M21: chunk streaming for large tilemaps.
struct TileChunk {
  int chunk_x = 0;
  int chunk_y = 0;
  int size = 16;
  std::vector<int> gids;
  bool resident = false;
};

class TilemapStreamer {
 public:
  void Configure(int map_w, int map_h, int chunk_size, std::size_t budget_chunks);
  void SetGid(int x, int y, int gid);
  void UpdateResidence(int focus_x, int focus_y, int radius_chunks);
  [[nodiscard]] std::size_t resident_count() const;
  [[nodiscard]] const TileChunk* FindChunk(int cx, int cy) const;

 private:
  int map_w_ = 0;
  int map_h_ = 0;
  int chunk_size_ = 16;
  std::size_t budget_ = 16;
  std::vector<int> gids_;
  std::vector<TileChunk> chunks_;
};

BonePose2D SampleSkeleton2D(const Skeleton2D& skel, float time);

}  // namespace engine::render2d
