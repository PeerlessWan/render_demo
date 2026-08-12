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

  [[nodiscard]] const std::vector<Line>& lines() const { return lines_; }

 private:
  std::vector<Line> lines_;
};

}  // namespace engine::debug
