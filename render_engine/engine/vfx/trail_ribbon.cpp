#include "engine/vfx/trail_ribbon.h"

#include <algorithm>

namespace engine::vfx {

void TrailRibbon::Configure(float lifetime, float width, int max_points) {
  lifetime_ = std::max(0.05f, lifetime);
  width_ = std::max(0.001f, width);
  max_points_ = std::max(2, max_points);
  if (static_cast<int>(points_.size()) > max_points_) {
    points_.erase(points_.begin(),
                  points_.begin() + (static_cast<int>(points_.size()) - max_points_));
  }
}

void TrailRibbon::Clear() { points_.clear(); }

void TrailRibbon::Push(const Vec3& position, ColorRgba color) {
  if (!enabled_) {
    return;
  }
  TrailPoint p;
  p.position = position;
  p.color = color;
  p.age = 0.f;
  points_.push_back(p);
  while (static_cast<int>(points_.size()) > max_points_) {
    points_.erase(points_.begin());
  }
}

void TrailRibbon::Step(float dt) {
  if (!enabled_) {
    points_.clear();
    return;
  }
  dt = std::max(0.f, dt);
  for (auto& p : points_) {
    p.age += dt;
    const float t = lifetime_ > 1e-5f ? std::clamp(1.f - p.age / lifetime_, 0.f, 1.f) : 0.f;
    p.color.a = t;
  }
  points_.erase(std::remove_if(points_.begin(), points_.end(),
                               [this](const TrailPoint& p) { return p.age >= lifetime_; }),
                points_.end());
}

std::vector<TrailSegment> TrailRibbon::BuildSegments() const {
  std::vector<TrailSegment> segs;
  if (points_.size() < 2) {
    return segs;
  }
  segs.reserve(points_.size() - 1);
  for (std::size_t i = 1; i < points_.size(); ++i) {
    TrailSegment s;
    s.a = points_[i - 1].position;
    s.b = points_[i].position;
    s.color = points_[i].color;
    s.color.a = 0.5f * (points_[i - 1].color.a + points_[i].color.a);
    s.width = width_;
    segs.push_back(s);
  }
  return segs;
}

void TrailRibbon::AppendDebugLines(debug::DebugDraw& draw) const {
  for (const auto& s : BuildSegments()) {
    draw.AddLine(s.a, s.b, s.color);
  }
}

}  // namespace engine::vfx
