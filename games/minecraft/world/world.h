#pragma once

#include "world/chunk.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace mc {

class World {
 public:
  explicit World(std::uint32_t seed = 1) : seed_(seed) {}

  [[nodiscard]] std::uint32_t seed() const { return seed_; }
  void set_seed(std::uint32_t s) { seed_ = s; }

  Chunk& EnsureChunk(ChunkCoord c);
  Chunk& InsertBlank(ChunkCoord c);
  [[nodiscard]] Chunk* FindChunk(ChunkCoord c);
  [[nodiscard]] const Chunk* FindChunk(ChunkCoord c) const;

  [[nodiscard]] Id Get(int x, int y, int z) const;
  void Set(int x, int y, int z, Id id);

  void StreamAround(int wx, int wz, int radius);
  [[nodiscard]] const std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>&
  chunks() const {
    return chunks_;
  }

  std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>& chunks() { return chunks_; }

 private:
  std::uint32_t seed_ = 1;
  std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks_;
};

}  // namespace mc
