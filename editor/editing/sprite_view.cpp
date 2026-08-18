#include "editing/sprite_view.h"

#include "editing/tile_edit.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace editor {
namespace {

engine::ColorRgba ColorForGid(int gid) {
  const float t = std::clamp(static_cast<float>(gid) / 8.f, 0.f, 1.f);
  return {0.25f + t * 0.55f, 0.45f + (1.f - t) * 0.35f, 0.85f - t * 0.4f, 0.92f};
}

bool AppendProjected(const engine::Mat4& view_proj, const engine::Vec3& origin, float size, float vw,
                     float vh, const engine::ColorRgba& color, int frame, std::string_view atlas,
                     int sort_layer, std::vector<engine::render2d::Sprite>* out) {
  engine::Vec2 pos;
  engine::Vec2 sz;
  if (!ProjectWorldQuad(view_proj, origin, size, vw, vh, &pos, &sz)) {
    return false;
  }
  engine::render2d::Sprite s;
  s.atlas_id = std::string(atlas);
  s.frame = frame;
  s.position = pos;
  s.size = sz;
  s.sort_layer = sort_layer;
  s.sort_y = pos.y + sz.y;
  s.color = color;
  out->push_back(std::move(s));
  return true;
}

}  // namespace

bool ProjectWorldToScreen(const engine::Mat4& view_proj, const engine::Vec3& world, float viewport_w,
                          float viewport_h, engine::Vec2* out) {
  if (!out || viewport_w < 1.f || viewport_h < 1.f) {
    return false;
  }
  const engine::Vec3 ndc = view_proj.TransformPoint(world);
  if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z)) {
    return false;
  }
  out->x = (ndc.x * 0.5f + 0.5f) * viewport_w;
  out->y = (1.f - (ndc.y * 0.5f + 0.5f)) * viewport_h;
  return true;
}

bool ProjectWorldQuad(const engine::Mat4& view_proj, const engine::Vec3& origin, float size,
                      float viewport_w, float viewport_h, engine::Vec2* pos, engine::Vec2* sz) {
  if (!pos || !sz) {
    return false;
  }
  const float h = size * 0.5f;
  const engine::Vec3 corners[4] = {
      {origin.x - h, origin.y, origin.z - h},
      {origin.x + h, origin.y, origin.z - h},
      {origin.x - h, origin.y, origin.z + h},
      {origin.x + h, origin.y, origin.z + h},
  };
  float min_x = 1e9f;
  float min_y = 1e9f;
  float max_x = -1e9f;
  float max_y = -1e9f;
  int ok = 0;
  for (const auto& c : corners) {
    engine::Vec2 p;
    if (!ProjectWorldToScreen(view_proj, c, viewport_w, viewport_h, &p)) {
      continue;
    }
    min_x = std::min(min_x, p.x);
    min_y = std::min(min_y, p.y);
    max_x = std::max(max_x, p.x);
    max_y = std::max(max_y, p.y);
    ++ok;
  }
  if (ok < 2) {
    return false;
  }
  pos->x = min_x;
  pos->y = min_y;
  sz->x = std::max(2.f, max_x - min_x);
  sz->y = std::max(2.f, max_y - min_y);
  return true;
}

void CollectProjectedSprites(const engine::scene::World& world, const std::vector<int>& tiles,
                             const engine::Mat4& view_proj, float viewport_w, float viewport_h,
                             std::vector<engine::render2d::Sprite>* out,
                             std::vector<engine::scene::NodeId>* node_ids) {
  if (!out) {
    return;
  }
  const int n = std::min(static_cast<int>(tiles.size()), kTileMap * kTileMap);
  for (int i = 0; i < n; ++i) {
    const int gid = tiles[static_cast<std::size_t>(i)];
    if (gid <= 0) {
      continue;
    }
    const int tx = i % kTileMap;
    const int ty = i / kTileMap;
    const engine::Vec3 origin{static_cast<float>(tx) - 7.5f, 0.05f,
                              static_cast<float>(ty) - 7.5f};
    if (AppendProjected(view_proj, origin, 1.f, viewport_w, viewport_h, ColorForGid(gid), gid, "tiles",
                        0, out)) {
      if (node_ids) {
        node_ids->push_back(engine::scene::kInvalidNode);
      }
    }
  }
  std::vector<engine::scene::NodeId> stack = world.roots();
  while (!stack.empty()) {
    const auto id = stack.back();
    stack.pop_back();
    if (!world.valid(id)) {
      continue;
    }
    for (auto c : world.children(id)) {
      stack.push_back(c);
    }
    const auto* spr = world.sprite(id);
    if (!spr) {
      continue;
    }
    const auto& m = world.world_matrix(id);
    const engine::Vec3 origin{m.m[12], m.m[13] + 0.05f, m.m[14]};
    const engine::ColorRgba col{0.95f, 0.75f, 0.2f, 0.95f};
    if (AppendProjected(view_proj, origin, 1.f, viewport_w, viewport_h, col, spr->gid,
                        spr->atlas_id.empty() ? "tiles" : spr->atlas_id, spr->sort_layer, out)) {
      if (node_ids) {
        node_ids->push_back(id);
      }
    }
  }
}

}  // namespace editor
