#include "engine/debug/debug_draw.h"

#include <algorithm>
#include <cmath>

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

void DebugDraw::AddGrid(float half_extent, float step, float y, const ColorRgba& color,
                        const ColorRgba& axis_tint) {
  if (half_extent <= 0.f || step <= 0.f) {
    return;
  }
  const float ext = half_extent;
  const int n = std::max(1, static_cast<int>(std::ceil(ext / step)));
  for (int i = -n; i <= n; ++i) {
    const float t = static_cast<float>(i) * step;
    if (t < -ext - 1e-4f || t > ext + 1e-4f) {
      continue;
    }
    const bool major = (i == 0);
    const ColorRgba c = major ? axis_tint : color;
    AddLine(Vec3{-ext, y, t}, Vec3{ext, y, t}, c);
    AddLine(Vec3{t, y, -ext}, Vec3{t, y, ext}, c);
  }
}

void DebugDraw::AddAxes(float length, float y) {
  const float L = std::max(length, 0.01f);
  AddLine(Vec3{0, y, 0}, Vec3{L, y, 0}, ColorRgba{0.95f, 0.2f, 0.2f, 1.f});
  AddLine(Vec3{0, y, 0}, Vec3{0, y + L, 0}, ColorRgba{0.25f, 0.9f, 0.3f, 1.f});
  AddLine(Vec3{0, y, 0}, Vec3{0, y, L}, ColorRgba{0.25f, 0.45f, 1.f, 1.f});
}

}  // namespace engine::debug
