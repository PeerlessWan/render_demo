#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/render2d/sprite.h"

#include <string>
#include <vector>

namespace engine::render2d {

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

// Tiny clip: per-bone rotation keys (degrees) over normalized time [0, duration].
struct BoneRotKey2D {
  float time = 0.f;
  float rot = 0.f;
};

struct SkeletonClip2D {
  std::string name;
  float duration = 1.f;
  bool loop = true;
  // channels[bone_index] = keys sorted by time (may be empty → bind pose).
  std::vector<std::vector<BoneRotKey2D>> channels;
};

// Options for expanding resident tile chunks into Sprite draw list (CPU path).
struct TileExpandDesc {
  float tile_w = 16.f;
  float tile_h = 16.f;
  int sort_layer = 0;
  std::string atlas_id = "tiles";
  bool skip_zero_gid = true;
  ColorRgba color{1.f, 1.f, 1.f, 1.f};
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
  [[nodiscard]] const std::vector<TileChunk>& chunks() const { return chunks_; }

  // Append one Sprite per non-empty tile in resident chunks (CPU expand for draw).
  void ExpandResidentToSprites(std::vector<Sprite>& out, const TileExpandDesc& desc = {}) const;

 private:
  int map_w_ = 0;
  int map_h_ = 0;
  int chunk_size_ = 16;
  std::size_t budget_ = 16;
  std::vector<int> gids_;
  std::vector<TileChunk> chunks_;
};

// Procedural wave sample (no clip data).
BonePose2D SampleSkeleton2D(const Skeleton2D& skel, float time);

// Sample with tiny clip keys; advances via caller-supplied time (loop when clip.loop).
BonePose2D SampleSkeletonClip2D(const Skeleton2D& skel, const SkeletonClip2D& clip, float time);

// Build a minimal 2-bone walk-ish clip for unit tests / learn smoke.
SkeletonClip2D MakeTinyWalkClip2D();

}  // namespace engine::render2d
