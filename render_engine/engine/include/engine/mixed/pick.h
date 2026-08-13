#pragma once

#include "engine/core/math.h"
#include "engine/render/render_scene.h"
#include "engine/render2d/sprite.h"
#include "engine/scene/world.h"

#include <vector>

namespace engine::mixed {

// M20: unified pick across 3D instances and 2D sprites.
struct PickHit {
  enum class Kind { None, Scene3D, Sprite2D } kind = Kind::None;
  scene::NodeId node = scene::kInvalidNode;
  int sprite_index = -1;
  float distance = 0.f;
};

struct PickQuery {
  Vec2 screen_px{};
  float viewport_w = 1.f;
  float viewport_h = 1.f;
  Mat4 inv_view_proj = Mat4::Identity();
};

// Ray vs AABB for 3D; point-in-rect for sprites (screen space). Prefer nearest 3D, else top sprite.
PickHit Pick(const std::vector<render::RenderInstance>& instances,
             const std::vector<render2d::Sprite>& sprites, const PickQuery& q);

// Pixel multi-DPI: integer scale factor from window size vs design resolution.
int IntegerScale(int window_w, int window_h, int design_w, int design_h);

}  // namespace engine::mixed
