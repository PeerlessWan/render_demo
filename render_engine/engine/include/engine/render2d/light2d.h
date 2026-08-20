#pragma once

#include "engine/core/math.h"
#include "engine/render2d/sprite.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::render2d {

enum class Light2DType : std::uint8_t { Point = 0, Directional = 1 };

// W21 / ADR 0044: Godot-style Light2D (point/directional). Layer mask & energy.
struct Light2D {
  Light2DType type = Light2DType::Point;
  Vec2 position{};
  Vec2 direction{0.f, -1.f};  // directional: toward lit surface
  ColorRgba color{1.f, 0.95f, 0.85f, 1.f};
  float energy = 1.f;
  float range = 128.f;  // point only
  std::uint32_t layer_mask = 0xFFFFFFFFu;
  bool enabled = true;
};

// Apply lights into Sprite::color (modulate × lighting). No lights → zero visual change
// beyond existing modulate. Optional fake normals: use UV-ish gradient when has_normals.
void ApplyLights2D(std::vector<Sprite>& sprites, std::span<const Light2D> lights);

// Layer visibility helper (CanvasItem-lite).
[[nodiscard]] bool SpriteVisibleOnLayers(const Sprite& s, std::uint32_t visible_mask);

}  // namespace engine::render2d
