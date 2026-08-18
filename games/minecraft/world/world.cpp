#include "world/world.h"

#include "world/gen.h"

namespace mc {

Chunk& World::InsertBlank(ChunkCoord c) {
  auto it = chunks_.find(c);
  if (it != chunks_.end()) {
    return *it->second;
  }
  auto ch = std::make_unique<Chunk>();
  Chunk& ref = *ch;
  chunks_.emplace(c, std::move(ch));
  return ref;
}

Chunk& World::EnsureChunk(ChunkCoord c) {
  auto it = chunks_.find(c);
  if (it != chunks_.end()) {
    return *it->second;
  }
  auto ch = std::make_unique<Chunk>();
  GenerateChunk(*ch, c, seed_);
  Chunk& ref = *ch;
  chunks_.emplace(c, std::move(ch));
  return ref;
}

Chunk* World::FindChunk(ChunkCoord c) {
  auto it = chunks_.find(c);
  return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk* World::FindChunk(ChunkCoord c) const {
  auto it = chunks_.find(c);
  return it == chunks_.end() ? nullptr : it->second.get();
}

Id World::Get(int x, int y, int z) const {
  if (y < 0 || y >= kChunkH) {
    return Id::Air;
  }
  ChunkCoord c{};
  int lx = 0;
  int lz = 0;
  WorldToChunk(x, z, &c, &lx, &lz);
  const Chunk* ch = FindChunk(c);
  if (!ch) {
    return Id::Air;
  }
  return ch->Get(lx, y, lz);
}

void World::Set(int x, int y, int z, Id id) {
  if (y < 0 || y >= kChunkH) {
    return;
  }
  ChunkCoord c{};
  int lx = 0;
  int lz = 0;
  WorldToChunk(x, z, &c, &lx, &lz);
  EnsureChunk(c).Set(lx, y, lz, id);
}

void World::StreamAround(int wx, int wz, int radius) {
  ChunkCoord center{};
  WorldToChunk(wx, wz, &center, nullptr, nullptr);
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      (void)EnsureChunk(ChunkCoord{center.x + dx, center.z + dz});
    }
  }
}

}  // namespace mc
