#pragma once

#include "engine/core/math.h"
#include "engine/debug/debug_draw.h"

#include <cstdint>
#include <vector>

namespace engine::vfx {

struct TrailPoint {
  Vec3 position{};
  ColorRgba color{0.95f, 0.55f, 0.2f, 1.f};
  float age = 0.f;
};

// World-space ribbon segment for debug draw or Sandbox screen-proxy quads.
struct TrailSegment {
  Vec3 a{};
  Vec3 b{};
  ColorRgba color{0.95f, 0.55f, 0.2f, 1.f};
  float width = 0.08f;
};

// M7 thin Trail API: CPU ring of points → segments / DebugDraw lines.
class TrailRibbon {
 public:
  void Configure(float lifetime, float width, int max_points);
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] float lifetime() const { return lifetime_; }
  [[nodiscard]] float width() const { return width_; }

  void Clear();
  void Push(const Vec3& position, ColorRgba color = {0.95f, 0.55f, 0.2f, 1.f});
  void Step(float dt);

  [[nodiscard]] const std::vector<TrailPoint>& points() const { return points_; }
  [[nodiscard]] std::vector<TrailSegment> BuildSegments() const;
  void AppendDebugLines(debug::DebugDraw& draw) const;

 private:
  bool enabled_ = true;
  float lifetime_ = 0.6f;
  float width_ = 0.08f;
  int max_points_ = 32;
  std::vector<TrailPoint> points_;
};

}  // namespace engine::vfx
