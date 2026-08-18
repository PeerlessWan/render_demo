#pragma once

#include "sim/inventory.h"

#include <cstdint>
#include <unordered_map>

namespace mc {

struct BlockPos {
  int x = 0;
  int y = 0;
  int z = 0;
  bool operator==(const BlockPos& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct BlockPosHash {
  std::size_t operator()(const BlockPos& p) const {
    return (static_cast<std::size_t>(p.x) * 73856093u) ^ (static_cast<std::size_t>(p.y) * 19349663u) ^
           (static_cast<std::size_t>(p.z) * 83492791u);
  }
};

struct ChestData {
  Stack slots[27]{};
};

struct FurnaceData {
  Stack input{};
  Stack fuel{};
  Stack output{};
  float cook = 0.f;
  float fuel_left = 0.f;
};

class Containers {
 public:
  std::unordered_map<BlockPos, ChestData, BlockPosHash> chests;
  std::unordered_map<BlockPos, FurnaceData, BlockPosHash> furnaces;

  ChestData& ChestAt(int x, int y, int z);
  FurnaceData& FurnaceAt(int x, int y, int z);
  void Remove(int x, int y, int z);
  void Tick(float dt);
};

}  // namespace mc
