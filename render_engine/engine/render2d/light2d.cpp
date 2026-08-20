#include "engine/render2d/light2d.h"

#include <algorithm>
#include <cmath>

namespace engine::render2d {
namespace {

bool PointInAabb(const Vec2& p, const Vec2& mn, const Vec2& mx) {
  return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y;
}

bool SegmentHitsAabb(const Vec2& a, const Vec2& b, const Vec2& mn, const Vec2& mx) {
  // Liang-Barsky style slab test.
  float t0 = 0.f;
  float t1 = 1.f;
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  auto clip = [&](float p, float q) {
    if (std::fabs(p) < 1e-8f) {
      return q >= 0.f;
    }
    const float r = q / p;
    if (p < 0.f) {
      if (r > t1) {
        return false;
      }
      if (r > t0) {
        t0 = r;
      }
    } else {
      if (r < t0) {
        return false;
      }
      if (r < t1) {
        t1 = r;
      }
    }
    return true;
  };
  return clip(-dx, a.x - mn.x) && clip(dx, mx.x - a.x) && clip(-dy, a.y - mn.y) &&
         clip(dy, mx.y - a.y) && t0 < t1;
}

bool OccluderBlocks(const Vec2& light_pos, const Vec2& target, const LightOccluder2D& occ) {
  if (!occ.enabled || occ.polygon.size() < 2) {
    return false;
  }
  if (occ.polygon.size() == 2) {
    Vec2 mn = occ.polygon[0];
    Vec2 mx = occ.polygon[1];
    if (mn.x > mx.x) {
      std::swap(mn.x, mx.x);
    }
    if (mn.y > mx.y) {
      std::swap(mn.y, mx.y);
    }
    // Don't shadow if sprite center is inside the occluder (self).
    if (PointInAabb(target, mn, mx)) {
      return false;
    }
    return SegmentHitsAabb(light_pos, target, mn, mx);
  }
  // Convex poly: any edge intersects light→target segment (excluding endpoints inside).
  const auto& poly = occ.polygon;
  for (std::size_t i = 0; i < poly.size(); ++i) {
    const Vec2& e0 = poly[i];
    const Vec2& e1 = poly[(i + 1) % poly.size()];
    // AABB of edge as thin slab approx via segment-AABB of edge bounds expanded.
    Vec2 mn{std::min(e0.x, e1.x), std::min(e0.y, e1.y)};
    Vec2 mx{std::max(e0.x, e1.x), std::max(e0.y, e1.y)};
    mn.x -= 0.5f;
    mn.y -= 0.5f;
    mx.x += 0.5f;
    mx.y += 0.5f;
    if (SegmentHitsAabb(light_pos, target, mn, mx)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool SpriteVisibleOnLayers(const Sprite& s, std::uint32_t visible_mask) {
  return (s.layer_mask & visible_mask) != 0;
}

float SampleOccluderShadow2D(const Vec2& sprite_center, const Light2D& light,
                             std::span<const LightOccluder2D> occluders) {
  if (!light.enabled || !light.cast_shadows || light.type != Light2DType::Point) {
    return 0.f;
  }
  for (const auto& occ : occluders) {
    if ((occ.layer_mask & light.layer_mask) == 0) {
      continue;
    }
    if (OccluderBlocks(light.position, sprite_center, occ)) {
      return 1.f;
    }
  }
  return 0.f;
}

void ApplyCanvasModulate(std::vector<Sprite>& sprites, const CanvasModulate& mod) {
  for (auto& s : sprites) {
    if (!SpriteVisibleOnLayers(s, mod.visible_layers)) {
      s.color.a = 0.f;
      continue;
    }
    s.color.r *= mod.color.r;
    s.color.g *= mod.color.g;
    s.color.b *= mod.color.b;
    s.color.a *= mod.color.a;
  }
}

void ApplyLights2D(std::vector<Sprite>& sprites, std::span<const Light2D> lights,
                   std::span<const LightOccluder2D> occluders) {
  if (sprites.empty()) {
    return;
  }
  bool any = false;
  for (const auto& L : lights) {
    if (L.enabled && L.energy > 1e-4f) {
      any = true;
      break;
    }
  }
  if (!any) {
    for (auto& s : sprites) {
      s.color.r *= s.modulate.r;
      s.color.g *= s.modulate.g;
      s.color.b *= s.modulate.b;
      s.color.a *= s.modulate.a;
    }
    return;
  }

  for (auto& s : sprites) {
    const float cx = s.position.x + s.size.x * 0.5f;
    const float cy = s.position.y + s.size.y * 0.5f;
    const Vec2 center{cx, cy};
    Vec2 n{0.f, 0.f};
    if (s.has_normals) {
      n = s.normal_tex.empty() ? Vec2{0.f, -1.f} : Vec2{0.15f, -0.95f};
      const float len = std::sqrt(n.x * n.x + n.y * n.y);
      if (len > 1e-4f) {
        n.x /= len;
        n.y /= len;
      }
    }

    ColorRgba lit{0.08f, 0.08f, 0.10f, 1.f};
    for (const auto& L : lights) {
      if (!L.enabled || L.energy <= 1e-4f) {
        continue;
      }
      if ((L.layer_mask & s.layer_mask) == 0) {
        continue;
      }
      const float shadow = SampleOccluderShadow2D(center, L, occluders);
      if (shadow >= 0.99f) {
        continue;
      }
      float atten = 0.f;
      Vec2 ldir{};
      if (L.type == Light2DType::Directional) {
        ldir = L.direction;
        const float len = std::sqrt(ldir.x * ldir.x + ldir.y * ldir.y);
        if (len > 1e-4f) {
          ldir.x /= len;
          ldir.y /= len;
        }
        atten = L.energy;
      } else {
        ldir = {L.position.x - cx, L.position.y - cy};
        const float dist = std::sqrt(ldir.x * ldir.x + ldir.y * ldir.y);
        if (dist >= L.range || L.range <= 1e-4f) {
          continue;
        }
        if (dist > 1e-4f) {
          ldir.x /= dist;
          ldir.y /= dist;
        }
        const float t = 1.f - dist / L.range;
        atten = L.energy * t * t;
      }
      float ndotl = 1.f;
      if (s.has_normals) {
        ndotl = std::max(0.f, n.x * (-ldir.x) + n.y * (-ldir.y));
        ndotl = 0.35f + 0.65f * ndotl;
      }
      const float w = atten * ndotl * (1.f - shadow);
      lit.r += L.color.r * w;
      lit.g += L.color.g * w;
      lit.b += L.color.b * w;
    }

    s.color.r = std::clamp(s.color.r * s.modulate.r * lit.r, 0.f, 4.f);
    s.color.g = std::clamp(s.color.g * s.modulate.g * lit.g, 0.f, 4.f);
    s.color.b = std::clamp(s.color.b * s.modulate.b * lit.b, 0.f, 4.f);
    s.color.a = std::clamp(s.color.a * s.modulate.a, 0.f, 1.f);
  }
}

}  // namespace engine::render2d
