#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <vector>

namespace engine::render2d {

// G13 / Mega-W8: lightweight 2D path → line-list or stroked triangle-strip mesh.
struct Path2DVertex {
  Vec2 position{};
  float u = 0.f;
  float v = 0.f;
};

enum class Path2DTopology {
  LineList = 0,
  TriangleStrip = 1,
  TriangleList = 2,
};

struct Path2DMesh {
  std::vector<Path2DVertex> vertices;
  std::vector<std::uint32_t> indices;
  Path2DTopology topology = Path2DTopology::LineList;
};

class Path2D {
 public:
  void Clear();
  void MoveTo(const Vec2& p);
  void LineTo(const Vec2& p);
  // Quadratic Bezier from current point through control to end (subdivides).
  void QuadraticTo(const Vec2& control, const Vec2& end, int segments = 8);

  [[nodiscard]] const std::vector<Vec2>& points() const { return points_; }

  // Degenerate segments skipped. Requires ≥2 points.
  [[nodiscard]] Path2DMesh BuildLineList() const;
  // Centerline stroke as triangle strip (2 verts per sample). half_width in same units as points.
  [[nodiscard]] Path2DMesh BuildStrokeTriangleStrip(float half_width) const;
  // Same stroke with U along length [0,1] and V across [-1,1]→[0,1] for textured ribbons.
  [[nodiscard]] Path2DMesh BuildTexturedStrokeTriangleStrip(float half_width) const;

  // Mega-W9 / G13: closed contour → triangle list (fan from first vertex; convex-friendly).
  [[nodiscard]] Path2DMesh TessellateFillFan() const;
  // Simple ear-clip for closed simple polygons (no holes / self-intersections).
  [[nodiscard]] Path2DMesh EarClipSimple() const;

 private:
  std::vector<Vec2> points_;
  bool have_current_ = false;
  Vec2 current_{};
};

}  // namespace engine::render2d
