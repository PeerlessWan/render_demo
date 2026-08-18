#include "world/gen.h"

#include "sim/blocks.h"

#include <cmath>

namespace mc {
namespace {

std::uint32_t Hash(int x, int z, std::uint32_t seed) {
  std::uint32_t h = seed;
  h ^= static_cast<std::uint32_t>(x) * 374761393u;
  h ^= static_cast<std::uint32_t>(z) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

float Noise2(int x, int z, std::uint32_t seed) {
  return static_cast<float>(Hash(x, z, seed) & 1023u) / 1023.f;
}

float SmoothHeight(int x, int z, std::uint32_t seed) {
  const int x0 = x >> 3;
  const int z0 = z >> 3;
  const float fx = static_cast<float>(x & 7) / 8.f;
  const float fz = static_cast<float>(z & 7) / 8.f;
  const float a = Noise2(x0, z0, seed);
  const float b = Noise2(x0 + 1, z0, seed);
  const float c = Noise2(x0, z0 + 1, seed);
  const float d = Noise2(x0 + 1, z0 + 1, seed);
  const float u = a + (b - a) * fx;
  const float v = c + (d - c) * fx;
  return u + (v - u) * fz;
}

bool CaveAt(int x, int y, int z, std::uint32_t seed) {
  const float n = Noise2(x + y * 3, z - y * 5, seed ^ 0x9e3779b9u);
  return n < 0.12f && y > 4 && y < 40;
}

void PlaceTree(Chunk& chunk, int lx, int y, int lz) {
  if (y + 5 >= kChunkH) {
    return;
  }
  for (int i = 1; i <= 4; ++i) {
    chunk.Set(lx, y + i, lz, Id::OakLog);
  }
  for (int dy = 3; dy <= 5; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      for (int dz = -2; dz <= 2; ++dz) {
        if (std::abs(dx) == 2 && std::abs(dz) == 2 && dy == 5) {
          continue;
        }
        if (dx == 0 && dz == 0 && dy < 5) {
          continue;
        }
        chunk.Set(lx + dx, y + dy, lz + dz, Id::OakLeaves);
      }
    }
  }
  chunk.Set(lx, y + 5, lz, Id::OakLeaves);
}

}  // namespace

void GenerateChunk(Chunk& chunk, ChunkCoord coord, std::uint32_t seed) {
  constexpr int kSea = 28;
  for (int lz = 0; lz < kChunkW; ++lz) {
    for (int lx = 0; lx < kChunkW; ++lx) {
      const int wx = coord.x * kChunkW + lx;
      const int wz = coord.z * kChunkW + lz;
      int h = 22 + static_cast<int>(SmoothHeight(wx, wz, seed) * 18.f);
      const float biome = SmoothHeight(wx >> 1, wz >> 1, seed ^ 0xabcdu);
      if (biome < 0.48f) {
        h = 18 + static_cast<int>(SmoothHeight(wx, wz, seed) * 10.f);
      } else {
        h = 26 + static_cast<int>(SmoothHeight(wx, wz, seed) * 22.f);
      }
      if (h < 8) {
        h = 8;
      }
      if (h > 56) {
        h = 56;
      }
      for (int y = 0; y < kChunkH; ++y) {
        Id id = Id::Air;
        if (y == 0) {
          id = Id::Bedrock;
        } else if (CaveAt(wx, y, wz, seed) && y < h) {
          id = y < kSea ? Id::Water : Id::Air;
        } else if (y > h) {
          id = (y <= kSea) ? Id::Water : Id::Air;
        } else if (y == h) {
          id = (h < kSea - 1) ? Id::Sand : Id::Grass;
        } else if (y >= h - 3) {
          id = (h < kSea - 1) ? Id::Sand : Id::Dirt;
        } else {
          id = Id::Stone;
          const auto ore = Hash(wx, y * 131 + wz, seed ^ 0x51u) & 63u;
          if (ore == 0 && y < 40) {
            id = Id::CoalOre;
          } else if (ore == 1 && y < 28) {
            id = Id::IronOre;
          } else if (ore == 2 && y < 18) {
            id = Id::Gravel;
          }
        }
        chunk.Set(lx, y, lz, id);
      }
      if (h >= kSea && (Hash(wx, wz, seed ^ 0x21u) & 63u) == 0) {
        if (chunk.Get(lx, h, lz) == Id::Grass) {
          PlaceTree(chunk, lx, h, lz);
        }
      }
    }
  }
  if ((Hash(coord.x, coord.z, seed ^ 0x51u) % 6u) == 0) {
    const int my = 12;
    for (int lx = 0; lx < kChunkW; ++lx) {
      for (int lz = 6; lz <= 9; ++lz) {
        chunk.Set(lx, my - 1, lz, Id::Cobble);
        chunk.Set(lx, my, lz, Id::Air);
        chunk.Set(lx, my + 1, lz, Id::Air);
        chunk.Set(lx, my + 2, lz, Id::Air);
        if ((lx & 3) == 0) {
          chunk.Set(lx, my - 1, lz, Id::CoalOre);
        }
      }
    }
  }
  chunk.set_dirty(true);
}

}  // namespace mc
