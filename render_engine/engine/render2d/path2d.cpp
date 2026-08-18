#include "engine/render2d/path2d.h"

#include <algorithm>
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

std::vector<Vec2> ClosedContour(const std::vector<Vec2>& pts) {
  std::vector<Vec2> out = pts;
  if (out.size() >= 2 && Dist(out.front(), out.back()) < 1e-5f) {
    out.pop_back();
  }
  return out;
}

float SignedArea(const std::vector<Vec2>& poly) {
  float a = 0.f;
  const std::size_t n = poly.size();
  for (std::size_t i = 0; i < n; ++i) {
    const auto& p = poly[i];
    const auto& q = poly[(i + 1) % n];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5f * a;
}

bool PointInTri(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
  const float c0 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
  const float c1 = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);
  const float c2 = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);
  const bool has_neg = (c0 < 0.f) || (c1 < 0.f) || (c2 < 0.f);
  const bool has_pos = (c0 > 0.f) || (c1 > 0.f) || (c2 > 0.f);
  return !(has_neg && has_pos);
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

Path2DMesh Path2D::TessellateFillFan() const {
  Path2DMesh mesh;
  mesh.topology = Path2DTopology::TriangleList;
  const auto poly = ClosedContour(points_);
  if (poly.size() < 3) {
    return mesh;
  }
  for (const auto& p : poly) {
    mesh.vertices.push_back({p, 0.f, 0.f});
  }
  for (std::uint32_t i = 1; i + 1 < static_cast<std::uint32_t>(poly.size()); ++i) {
    mesh.indices.push_back(0);
    mesh.indices.push_back(i);
    mesh.indices.push_back(i + 1);
  }
  return mesh;
}

Path2DMesh Path2D::EarClipSimple() const {
  Path2DMesh mesh;
  mesh.topology = Path2DTopology::TriangleList;
  auto poly = ClosedContour(points_);
  if (poly.size() < 3) {
    return mesh;
  }
  // Ensure CCW for consistent ear tests.
  if (SignedArea(poly) < 0.f) {
    std::reverse(poly.begin(), poly.end());
  }
  for (const auto& p : poly) {
    mesh.vertices.push_back({p, 0.f, 0.f});
  }

  std::vector<std::uint32_t> idx(poly.size());
  for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(poly.size()); ++i) {
    idx[i] = i;
  }

  auto is_ear = [&](std::size_t i0, std::size_t i1, std::size_t i2) {
    const Vec2& a = poly[idx[i0]];
    const Vec2& b = poly[idx[i1]];
    const Vec2& c = poly[idx[i2]];
    // Convex tip (CCW).
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross <= 1e-8f) {
      return false;
    }
    for (std::size_t k = 0; k < idx.size(); ++k) {
      if (k == i0 || k == i1 || k == i2) {
        continue;
      }
      if (PointInTri(poly[idx[k]], a, b, c)) {
        return false;
      }
    }
    return true;
  };

  int guard = static_cast<int>(idx.size()) * static_cast<int>(idx.size());
  while (idx.size() > 3 && guard-- > 0) {
    bool clipped = false;
    const std::size_t n = idx.size();
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t i0 = (i + n - 1) % n;
      const std::size_t i1 = i;
      const std::size_t i2 = (i + 1) % n;
      if (!is_ear(i0, i1, i2)) {
        continue;
      }
      mesh.indices.push_back(idx[i0]);
      mesh.indices.push_back(idx[i1]);
      mesh.indices.push_back(idx[i2]);
      idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i1));
      clipped = true;
      break;
    }
    if (!clipped) {
      break;
    }
  }
  if (idx.size() == 3) {
    mesh.indices.push_back(idx[0]);
    mesh.indices.push_back(idx[1]);
    mesh.indices.push_back(idx[2]);
  }
  return mesh;
}

}  // namespace engine::render2d
