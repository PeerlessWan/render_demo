#pragma once

#include "engine/core/math.h"
#include "engine/render2d/sprite.h"
#include "engine/scene/world.h"

#include <vector>

namespace editor {

[[nodiscard]] bool ProjectWorldToScreen(const engine::Mat4& view_proj, const engine::Vec3& world,
                                        float viewport_w, float viewport_h, engine::Vec2* out);

// XZ quad of world `size` centered at `origin` → screen rect (position + size in pixels).
[[nodiscard]] bool ProjectWorldQuad(const engine::Mat4& view_proj, const engine::Vec3& origin, float size,
                                    float viewport_w, float viewport_h, engine::Vec2* pos,
                                    engine::Vec2* sz);

// Tile grid (XZ, GID>0) then Sprite nodes. `node_ids` aligns 1:1; tiles use kInvalidNode.
void CollectProjectedSprites(const engine::scene::World& world, const std::vector<int>& tiles,
                             const engine::Mat4& view_proj, float viewport_w, float viewport_h,
                             std::vector<engine::render2d::Sprite>* out,
                             std::vector<engine::scene::NodeId>* node_ids);

}  // namespace editor
