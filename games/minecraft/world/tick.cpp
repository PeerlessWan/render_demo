#include "world/tick.h"

#include "sim/blocks.h"
#include "world/chunk.h"

#include <cmath>
#include <vector>

namespace mc {

void TickFallingAndWater(World& world, int px, int pz, int chunk_radius) {
  ChunkCoord center{};
  WorldToChunk(px, pz, &center, nullptr, nullptr);
  int falls = 0;
  for (int dz = -chunk_radius; dz <= chunk_radius; ++dz) {
    for (int dx = -chunk_radius; dx <= chunk_radius; ++dx) {
      const ChunkCoord cc{center.x + dx, center.z + dz};
      Chunk* ch = world.FindChunk(cc);
      if (!ch) {
        continue;
      }
      const int ox = cc.x * kChunkW;
      const int oz = cc.z * kChunkW;
      for (int y = 1; y < kChunkH; ++y) {
        for (int lz = 0; lz < kChunkW; ++lz) {
          for (int lx = 0; lx < kChunkW; ++lx) {
            const Id id = ch->Get(lx, y, lz);
            if (!Falls(id) || falls >= 48) {
              continue;
            }
            const int x = ox + lx;
            const int z = oz + lz;
            const Id below = world.Get(x, y - 1, z);
            if (below == Id::Air || below == Id::Water) {
              world.Set(x, y - 1, z, id);
              world.Set(x, y, z, Id::Air);
              ++falls;
            }
          }
        }
      }
    }
  }

  struct Cell {
    int x, y, z;
  };
  std::vector<Cell> add;
  add.reserve(32);
  constexpr int kMaxWater = 24;
  for (int dz = -chunk_radius; dz <= chunk_radius && static_cast<int>(add.size()) < kMaxWater; ++dz) {
    for (int dx = -chunk_radius; dx <= chunk_radius && static_cast<int>(add.size()) < kMaxWater; ++dx) {
      const ChunkCoord cc{center.x + dx, center.z + dz};
      const Chunk* ch = world.FindChunk(cc);
      if (!ch) {
        continue;
      }
      const int ox = cc.x * kChunkW;
      const int oz = cc.z * kChunkW;
      for (int y = 1; y < kChunkH && static_cast<int>(add.size()) < kMaxWater; ++y) {
        for (int lz = 0; lz < kChunkW; ++lz) {
          for (int lx = 0; lx < kChunkW; ++lx) {
            if (ch->Get(lx, y, lz) != Id::Water) {
              continue;
            }
            const int x = ox + lx;
            const int z = oz + lz;
            if (world.Get(x, y - 1, z) == Id::Air) {
              add.push_back({x, y - 1, z});
              continue;
            }
            const Id under = world.Get(x, y - 1, z);
            if (!IsSolid(under) && under != Id::Water) {
              continue;
            }
            static const int kDir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& d : kDir) {
              const int nx = x + d[0];
              const int nz = z + d[1];
              if (world.Get(nx, y, nz) != Id::Air) {
                continue;
              }
              const Id nb = world.Get(nx, y - 1, nz);
              if (IsSolid(nb) || nb == Id::Water) {
                add.push_back({nx, y, nz});
                break;
              }
            }
          }
        }
      }
    }
  }
  for (const auto& c : add) {
    if (world.Get(c.x, c.y, c.z) == Id::Air) {
      world.Set(c.x, c.y, c.z, Id::Water);
    }
  }
}

}  // namespace mc
