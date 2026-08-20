#pragma once

#include "engine/core/math.h"
#include "engine/render2d/sprite.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::render2d {

enum class Light2DType : std::uint8_t { Point = 0, Directional = 1 };

struct Light2D {
  Light2DType type = Light2DType::Point;
  Vec2 position{};
  Vec2 direction{0.f, -1.f};
  ColorRgba color{1.f, 0.95f, 0.85f, 1.f};
  float energy = 1.f;
  float range = 128.f;
  std::uint32_t layer_mask = 0xFFFFFFFFu;
  bool enabled = true;
  bool cast_shadows = true;
};

// W22: Godot LightOccluder2D-lite (axis-aligned rect or convex poly in screen space).
struct LightOccluder2D {
  std::vector<Vec2> polygon;  // if size==2 treated as AABB min/max; else convex poly
  std::uint32_t layer_mask = 0xFFFFFFFFu;
  bool enabled = true;
};

struct CanvasModulate {
  ColorRgba color{1.f, 1.f, 1.f, 1.f};
  std::uint32_t visible_layers = 0xFFFFFFFFu;
};

void ApplyLights2D(std::vector<Sprite>& sprites, std::span<const Light2D> lights,
                   std::span<const LightOccluder2D> occluders = {});

void ApplyCanvasModulate(std::vector<Sprite>& sprites, const CanvasModulate& mod);

[[nodiscard]] bool SpriteVisibleOnLayers(const Sprite& s, std::uint32_t visible_mask);

// 0 = fully lit, 1 = fully in shadow (point lights only).
[[nodiscard]] float SampleOccluderShadow2D(const Vec2& sprite_center, const Light2D& light,
                                           std::span<const LightOccluder2D> occluders);

}  // namespace engine::render2d
