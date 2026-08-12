#include "engine/debug/debug_draw.h"

namespace engine::debug {

void DebugDraw::Clear() { lines_.clear(); }

void DebugDraw::AddLine(const Vec3& a, const Vec3& b, const ColorRgba& color) {
  lines_.push_back(Line{a, b, color});
}

void DebugDraw::AddAabb(const engine::Aabb& box, const ColorRgba& color) {
  const Vec3& mn = box.min;
  const Vec3& mx = box.max;
  const Vec3 c[8] = {
      {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
      {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
  };
  const int e[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                      {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (const auto& edge : e) {
    AddLine(c[edge[0]], c[edge[1]], color);
  }
}

}  // namespace engine::debug
