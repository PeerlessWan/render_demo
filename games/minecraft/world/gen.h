#pragma once

#include "world/chunk.h"

#include <cstdint>

namespace mc {

void GenerateChunk(Chunk& chunk, ChunkCoord coord, std::uint32_t seed);

}  // namespace mc
