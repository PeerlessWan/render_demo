#include "engine/render2d/light2d.h"

#include <algorithm>
#include <cmath>

namespace engine::render2d {

bool SpriteVisibleOnLayers(const Sprite& s, std::uint32_t visible_mask) {
  return (s.layer_mask & visible_mask) != 0;
}

void ApplyLights2D(std::vector<Sprite>& sprites, std::span<const Light2D> lights) {
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
    // Still apply modulate into color when set (identity modulate is no-op).
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
    // Fake normal from sprite UV center → slight dome when has_normals.
    Vec2 n{0.f, 0.f};
    if (s.has_normals) {
      n = {0.f, -1.f};
      if (!s.normal_tex.empty()) {
        // Hint: normal_tex present → tilt toward screen-up for demo readability.
        n = {0.15f, -0.95f};
      }
      const float len = std::sqrt(n.x * n.x + n.y * n.y);
      if (len > 1e-4f) {
        n.x /= len;
        n.y /= len;
      }
    }

    ColorRgba lit{0.08f, 0.08f, 0.10f, 1.f};  // small ambient so unlit sprites stay visible
    for (const auto& L : lights) {
      if (!L.enabled || L.energy <= 1e-4f) {
        continue;
      }
      if ((L.layer_mask & s.layer_mask) == 0) {
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
      const float w = atten * ndotl;
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
