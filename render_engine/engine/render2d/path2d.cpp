#include "engine/render2d/path2d.h"

#include <cmath>

namespace engine::render2d {
namespace {

Vec2 Lerp(const Vec2& a, const Vec2& b, float t) {
  return Vec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

Vec2 QuadPoint(const Vec2& p0, const Vec2& p1, const Vec2& p2, float t) {
  const Vec2 a = Lerp(p0, p1, t);
  const Vec2 b = Lerp(p1, p2, t);
  return Lerp(a, b, t);
}

float Dist(const Vec2& a, const Vec2& b) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  return std::sqrt(dx * dx + dy * dy);
}

Vec2 PerpNorm(const Vec2& dir) {
  const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
  if (len < 1e-8f) {
    return Vec2{0.f, 1.f};
  }
  return Vec2{-dir.y / len, dir.x / len};
}

}  // namespace

void Path2D::Clear() {
  points_.clear();
  have_current_ = false;
  current_ = {};
}

void Path2D::MoveTo(const Vec2& p) {
  current_ = p;
  have_current_ = true;
  if (points_.empty() || Dist(points_.back(), p) > 1e-6f) {
    points_.push_back(p);
  }
}

void Path2D::LineTo(const Vec2& p) {
  if (!have_current_) {
    MoveTo(p);
    return;
  }
  if (Dist(current_, p) > 1e-6f) {
    points_.push_back(p);
  }
  current_ = p;
  have_current_ = true;
}

void Path2D::QuadraticTo(const Vec2& control, const Vec2& end, int segments) {
  if (!have_current_) {
    MoveTo(control);
  }
  const Vec2 p0 = current_;
  const int segs = segments < 1 ? 1 : segments;
  for (int i = 1; i <= segs; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(segs);
    LineTo(QuadPoint(p0, control, end, t));
  }
}

Path2DMesh Path2D::BuildLineList() const {
  Path2DMesh mesh;
  mesh.topology = Path2DTopology::LineList;
  if (points_.size() < 2) {
    return mesh;
  }
  for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({points_[i], 0.f, 0.f});
    mesh.vertices.push_back({points_[i + 1], 1.f, 0.f});
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1);
  }
  return mesh;
}

Path2DMesh Path2D::BuildStrokeTriangleStrip(float half_width) const {
  return BuildTexturedStrokeTriangleStrip(half_width);
}

Path2DMesh Path2D::BuildTexturedStrokeTriangleStrip(float half_width) const {
  Path2DMesh mesh;
  mesh.topology = Path2DTopology::TriangleStrip;
  if (points_.size() < 2 || half_width <= 0.f) {
    return mesh;
  }
  float total = 0.f;
  std::vector<float> cum(points_.size(), 0.f);
  for (std::size_t i = 1; i < points_.size(); ++i) {
    total += Dist(points_[i - 1], points_[i]);
    cum[i] = total;
  }
  const float inv_total = total > 1e-6f ? 1.f / total : 0.f;

  for (std::size_t i = 0; i < points_.size(); ++i) {
    Vec2 tan{};
    if (i + 1 < points_.size()) {
      tan = Vec2{points_[i + 1].x - points_[i].x, points_[i + 1].y - points_[i].y};
    } else {
      tan = Vec2{points_[i].x - points_[i - 1].x, points_[i].y - points_[i - 1].y};
    }
    if (i > 0 && i + 1 < points_.size()) {
      const Vec2 a{points_[i].x - points_[i - 1].x, points_[i].y - points_[i - 1].y};
      const Vec2 b{points_[i + 1].x - points_[i].x, points_[i + 1].y - points_[i].y};
      tan = Vec2{a.x + b.x, a.y + b.y};
    }
    const Vec2 n = PerpNorm(tan);
    const float u = cum[i] * inv_total;
    const Vec2 left{points_[i].x + n.x * half_width, points_[i].y + n.y * half_width};
    const Vec2 right{points_[i].x - n.x * half_width, points_[i].y - n.y * half_width};
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({left, u, 0.f});
    mesh.vertices.push_back({right, u, 1.f});
    if (i > 0) {
      // Degenerate-free strip: emit two tris via indices (strip order L0 R0 L1 R1 …).
      mesh.indices.push_back(base - 2);
      mesh.indices.push_back(base - 1);
      mesh.indices.push_back(base);
      mesh.indices.push_back(base - 1);
      mesh.indices.push_back(base + 1);
      mesh.indices.push_back(base);
    }
  }
  return mesh;
}

}  // namespace engine::render2d
