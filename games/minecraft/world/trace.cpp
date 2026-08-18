#include "world/trace.h"

#include "sim/blocks.h"

#include <cmath>

namespace mc {

TraceHit TraceBlocks(const World& world, engine::Vec3 origin, engine::Vec3 dir, float max_dist) {
  TraceHit out;
  const float len = dir.length();
  if (len < 1e-6f) {
    return out;
  }
  dir = dir * (1.f / len);
  int x = static_cast<int>(std::floor(origin.x));
  int y = static_cast<int>(std::floor(origin.y));
  int z = static_cast<int>(std::floor(origin.z));
  const int step_x = dir.x > 0.f ? 1 : (dir.x < 0.f ? -1 : 0);
  const int step_y = dir.y > 0.f ? 1 : (dir.y < 0.f ? -1 : 0);
  const int step_z = dir.z > 0.f ? 1 : (dir.z < 0.f ? -1 : 0);
  auto t_max_axis = [](float o, float d, int v, int step) {
    if (std::fabs(d) < 1e-8f) {
      return 1e30f;
    }
    const float next = step > 0 ? static_cast<float>(v + 1) : static_cast<float>(v);
    return (next - o) / d;
  };
  float t_max_x = t_max_axis(origin.x, dir.x, x, step_x);
  float t_max_y = t_max_axis(origin.y, dir.y, y, step_y);
  float t_max_z = t_max_axis(origin.z, dir.z, z, step_z);
  const float t_delta_x = step_x != 0 ? std::fabs(1.f / dir.x) : 1e30f;
  const float t_delta_y = step_y != 0 ? std::fabs(1.f / dir.y) : 1e30f;
  const float t_delta_z = step_z != 0 ? std::fabs(1.f / dir.z) : 1e30f;
  float t = 0.f;
  int px = x;
  int py = y;
  int pz = z;
  for (int i = 0; i < 96 && t <= max_dist; ++i) {
    const Id id = world.Get(x, y, z);
    if (id != Id::Air && id != Id::Water && IsBlock(id)) {
      out.hit = true;
      out.x = x;
      out.y = y;
      out.z = z;
      out.px = px;
      out.py = py;
      out.pz = pz;
      out.dist = t;
      return out;
    }
    px = x;
    py = y;
    pz = z;
    if (t_max_x < t_max_y && t_max_x < t_max_z) {
      t = t_max_x;
      t_max_x += t_delta_x;
      x += step_x;
    } else if (t_max_y < t_max_z) {
      t = t_max_y;
      t_max_y += t_delta_y;
      y += step_y;
    } else {
      t = t_max_z;
      t_max_z += t_delta_z;
      z += step_z;
    }
  }
  return out;
}

}  // namespace mc
