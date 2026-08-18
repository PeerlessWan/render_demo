#pragma once

#include "world/world.h"

#include "engine/core/math.h"

namespace mc {

struct TraceHit {
  bool hit = false;
  int x = 0;
  int y = 0;
  int z = 0;
  int px = 0;
  int py = 0;
  int pz = 0;
  float dist = 0.f;
};

TraceHit TraceBlocks(const World& world, engine::Vec3 origin, engine::Vec3 dir, float max_dist);

}  // namespace mc
