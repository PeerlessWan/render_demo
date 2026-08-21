#pragma once

#include "engine/core/math.h"
#include "engine/render2d/fx2d.h"

namespace engine::render2d {

// Godot Camera2D lite (ADR 0049).
struct Camera2D {
  Vec2 position{};
  Vec2 offset{};
  float zoom = 1.f;
  float rotation = 0.f;
  Vec2 design_size{320.f, 180.f};
  bool integer_scale = true;
  Vec2 limit_min{-1e6f, -1e6f};
  Vec2 limit_max{1e6f, 1e6f};
  float follow_smoothing = 0.f;  // 0 = snap
  CameraShake2D shake{};
};

void Camera2DFollow(Camera2D* cam, const Vec2& target, float dt);
void Camera2DClampLimits(Camera2D* cam);

// Maps world → screen pixels for a viewport of size view_w x view_h.
[[nodiscard]] Vec2 Camera2DWorldToScreen(const Camera2D& cam, Vec2 world, float view_w,
                                         float view_h);
[[nodiscard]] Vec2 Camera2DScreenToWorld(const Camera2D& cam, Vec2 screen, float view_w,
                                         float view_h);

// Effective pixels-per-world-unit after integer scale (min scale that fits design).
[[nodiscard]] float Camera2DPixelsPerUnit(const Camera2D& cam, float view_w, float view_h);

}  // namespace engine::render2d
