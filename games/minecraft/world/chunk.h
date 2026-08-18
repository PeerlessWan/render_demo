#pragma once

#include "sim/blocks.h"

#include <cstdint>
#include <vector>

namespace mc {

inline constexpr int kChunkW = 16;
inline constexpr int kChunkH = 64;

struct ChunkCoord {
  int x = 0;
  int z = 0;
  bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
};

struct ChunkCoordHash {
  std::size_t operator()(const ChunkCoord& c) const {
    return (static_cast<std::size_t>(c.x) * 73856093u) ^ (static_cast<std::size_t>(c.z) * 19349663u);
  }
};

class Chunk {
 public:
  Chunk() : blocks_(static_cast<std::size_t>(kChunkW * kChunkW * kChunkH), Id::Air) {}

  [[nodiscard]] static int Index(int lx, int y, int lz) {
    return lx + lz * kChunkW + y * (kChunkW * kChunkW);
  }

  [[nodiscard]] Id Get(int lx, int y, int lz) const {
    if (lx < 0 || lz < 0 || y < 0 || lx >= kChunkW || lz >= kChunkW || y >= kChunkH) {
      return Id::Air;
    }
    return blocks_[static_cast<std::size_t>(Index(lx, y, lz))];
  }

  void Set(int lx, int y, int lz, Id id) {
    if (lx < 0 || lz < 0 || y < 0 || lx >= kChunkW || lz >= kChunkW || y >= kChunkH) {
      return;
    }
    blocks_[static_cast<std::size_t>(Index(lx, y, lz))] = id;
    dirty_ = true;
  }

  [[nodiscard]] const std::vector<Id>& data() const { return blocks_; }
  std::vector<Id>& data() { return blocks_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  void set_dirty(bool v) { dirty_ = v; }

 private:
  std::vector<Id> blocks_;
  bool dirty_ = true;
};

inline void WorldToChunk(int x, int z, ChunkCoord* c, int* lx, int* lz) {
  int cx = x >> 4;
  int cz = z >> 4;
  int local_x = x - (cx * kChunkW);
  int local_z = z - (cz * kChunkW);
  if (local_x < 0) {
    --cx;
    local_x += kChunkW;
  }
  if (local_z < 0) {
    --cz;
    local_z += kChunkW;
  }
  if (c) {
    *c = ChunkCoord{cx, cz};
  }
  if (lx) {
    *lx = local_x;
  }
  if (lz) {
    *lz = local_z;
  }
}

}  // namespace mc
