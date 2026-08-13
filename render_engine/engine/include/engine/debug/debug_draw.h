#pragma once

#include "engine/core/math.h"

#include <vector>

namespace engine::debug {

struct Line {
  Vec3 a{};
  Vec3 b{};
  ColorRgba color{1, 1, 0, 1};
};

class DebugDraw {
 public:
  void Clear();
  void AddLine(const Vec3& a, const Vec3& b, const ColorRgba& color = {1, 1, 0, 1});
  void AddAabb(const engine::Aabb& box, const ColorRgba& color = {0, 1, 0, 1});

  // XZ ground grid centered at origin (y = height).
  void AddGrid(float half_extent, float step, float y = 0.f,
               const ColorRgba& color = {0.35f, 0.38f, 0.42f, 1.f},
               const ColorRgba& axis_tint = {0.55f, 0.58f, 0.62f, 1.f});

  // RGB axes from origin (X=red, Y=green, Z=blue).
  void AddAxes(float length = 2.f, float y = 0.02f);

  [[nodiscard]] const std::vector<Line>& lines() const { return lines_; }

 private:
  std::vector<Line> lines_;
};

}  // namespace engine::debug
