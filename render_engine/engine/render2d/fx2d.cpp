#include "engine/render2d/fx2d.h"

#include <algorithm>
#include <cmath>

namespace engine::render2d {

ColorRgba FogTintColor(const ColorRgba& src, const ColorRgba& fog, float amount) {
  const float a = std::clamp(amount, 0.f, 1.f);
  return ColorRgba{src.r + (fog.r - src.r) * a, src.g + (fog.g - src.g) * a,
                   src.b + (fog.b - src.b) * a, src.a};
}

void ApplyFogTint2D(std::vector<Sprite>& sprites, const ColorRgba& fog, float amount) {
  for (auto& s : sprites) {
    s.color = FogTintColor(s.color, fog, amount);
  }
}

void CameraShake2D::AddTrauma(float t) {
  trauma = std::clamp(trauma + t, 0.f, 1.f);
}

void CameraShake2D::Step(float dt) {
  time += dt;
  trauma = std::max(0.f, trauma - decay_per_sec * dt);
}

Vec2 CameraShake2D::Offset() const {
  if (trauma <= 1e-5f) {
    return {};
  }
  const float mag = trauma * trauma * max_offset;
  // Deterministic pseudo-noise from time (no RNG dependency).
  const float ox = std::sin(time * 37.1f) * mag;
  const float oy = std::cos(time * 29.7f) * mag;
  return {ox, oy};
}

}  // namespace engine::render2d
