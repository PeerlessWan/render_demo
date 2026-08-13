#include "engine/mixed/pick.h"

#include "engine/render/render_scene.h"

#include <algorithm>
#include <cmath>

namespace engine::mixed {
namespace {

bool RayAabb(const Vec3& origin, const Vec3& dir, const Aabb& box, float& t_hit) {
  float tmin = 0.f;
  float tmax = 1e9f;
  const float* o = &origin.x;
  const float* d = &dir.x;
  const float* mn = &box.min.x;
  const float* mx = &box.max.x;
  for (int a = 0; a < 3; ++a) {
    if (!std::isfinite(o[a]) || !std::isfinite(d[a]) || !std::isfinite(mn[a]) ||
        !std::isfinite(mx[a])) {
      return false;
    }
    if (std::fabs(d[a]) < 1e-8f) {
      if (o[a] < mn[a] || o[a] > mx[a]) {
        return false;
      }
      continue;
    }
    float t1 = (mn[a] - o[a]) / d[a];
    float t2 = (mx[a] - o[a]) / d[a];
    if (t1 > t2) {
      std::swap(t1, t2);
    }
    tmin = std::max(tmin, t1);
    tmax = std::min(tmax, t2);
    if (tmin > tmax) {
      return false;
    }
  }
  t_hit = tmin;
  return tmin >= 0.f && std::isfinite(tmin);
}

}  // namespace

PickHit Pick(const std::vector<render::RenderInstance>& instances,
             const std::vector<render2d::Sprite>& sprites, const PickQuery& q) {
  PickHit best;

  // Screen point -> NDC -> world ray.
  const float ndc_x = (q.screen_px.x / q.viewport_w) * 2.f - 1.f;
  const float ndc_y = 1.f - (q.screen_px.y / q.viewport_h) * 2.f;
  const Vec3 near_p = q.inv_view_proj.TransformPoint({ndc_x, ndc_y, 0.f});
  const Vec3 far_p = q.inv_view_proj.TransformPoint({ndc_x, ndc_y, 1.f});
  Vec3 dir = far_p - near_p;
  if (!std::isfinite(near_p.x) || !std::isfinite(far_p.x) || dir.length_squared() < 1e-12f) {
    return best;
  }
  dir = Normalize(dir);

  float best_t = 1e9f;
  for (const auto& inst : instances) {
    float t = 0.f;
    if (RayAabb(near_p, dir, inst.world_bounds, t) && t < best_t) {
      best_t = t;
      best.kind = PickHit::Kind::Scene3D;
      best.node = inst.node;
      best.distance = t;
      best.sprite_index = -1;
    }
  }
  if (best.kind != PickHit::Kind::None) {
    return best;
  }

  // Top-most sprite under cursor (last in sorted list wins).
  for (int i = static_cast<int>(sprites.size()) - 1; i >= 0; --i) {
    const auto& s = sprites[static_cast<std::size_t>(i)];
    if (q.screen_px.x >= s.position.x && q.screen_px.x <= s.position.x + s.size.x &&
        q.screen_px.y >= s.position.y && q.screen_px.y <= s.position.y + s.size.y) {
      best.kind = PickHit::Kind::Sprite2D;
      best.sprite_index = i;
      best.distance = 0.f;
      return best;
    }
  }
  return best;
}

int IntegerScale(int window_w, int window_h, int design_w, int design_h) {
  if (design_w <= 0 || design_h <= 0) {
    return 1;
  }
  const int sx = window_w / design_w;
  const int sy = window_h / design_h;
  const int s = std::min(sx, sy);
  return s < 1 ? 1 : s;
}

}  // namespace engine::mixed
