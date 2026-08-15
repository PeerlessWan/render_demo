#pragma once

#include "engine/core/math.h"
#include "engine/render2d/sprite.h"

#include <vector>

namespace engine::render2d {

// M21: tint sprites toward fog color by amount in [0,1] (alpha preserved).
void ApplyFogTint2D(std::vector<Sprite>& sprites, const ColorRgba& fog, float amount);
[[nodiscard]] ColorRgba FogTintColor(const ColorRgba& src, const ColorRgba& fog, float amount);

// M21: simple camera shake — accumulate / decay offset for 2D or 3D camera position.
struct CameraShake2D {
  float trauma = 0.f;       // [0,1] intensity budget
  float decay_per_sec = 1.2f;
  float max_offset = 8.f;   // pixels (2D) or world units when used in 3D
  float time = 0.f;

  void AddTrauma(float t);
  void Step(float dt);
  [[nodiscard]] Vec2 Offset() const;
};

}  // namespace engine::render2d
