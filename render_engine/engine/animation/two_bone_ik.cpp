#include "engine/animation/two_bone_ik.h"

#include <algorithm>
#include <cmath>

namespace engine::animation {
namespace {

float Clamp(float v, float lo, float hi) { return std::clamp(v, lo, hi); }

Vec3 Perpendicular(const Vec3& dir) {
  const Vec3 hint = std::fabs(dir.y) < 0.9f ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
  return Normalize(Cross(dir, hint));
}

}  // namespace

TwoBoneIkOutput SolveTwoBoneIK(const TwoBoneIkInput& in) {
  TwoBoneIkOutput out;
  out.mid = in.mid;
  out.tip = in.tip;

  const float len_a = (in.mid - in.root).length();
  const float len_b = (in.tip - in.mid).length();
  if (len_a < 1e-6f || len_b < 1e-6f) {
    return out;
  }

  const float max_reach = len_a + len_b;
  const float min_reach = std::fabs(len_a - len_b);

  Vec3 to_target = in.target - in.root;
  float target_dist = to_target.length();
  const bool originally_reachable =
      target_dist >= min_reach - 1e-4f && target_dist <= max_reach + 1e-4f;

  if (target_dist < 1e-6f) {
    Vec3 axis = Normalize(in.pole - in.root);
    if (axis.length_squared() < 1e-8f) {
      axis = Vec3{0.f, 1.f, 0.f};
    }
    out.mid = in.root + axis * len_a;
    out.tip = out.mid;
    out.reached = originally_reachable;
    return out;
  }

  const float dist = Clamp(target_dist, min_reach + 1e-5f, max_reach - 1e-5f);
  const Vec3 dir = to_target * (1.f / target_dist);

  Vec3 bend = Cross(dir, Cross(in.pole - in.root, dir));
  if (bend.length_squared() < 1e-10f) {
    bend = Perpendicular(dir);
  } else {
    bend = Normalize(bend);
  }

  const float cos_a =
      Clamp((len_a * len_a + dist * dist - len_b * len_b) / (2.f * len_a * dist), -1.f, 1.f);
  const float sin_a = std::sqrt(std::max(0.f, 1.f - cos_a * cos_a));

  out.mid = in.root + dir * (len_a * cos_a) + bend * (len_a * sin_a);
  out.tip = in.root + dir * dist;
  out.reached = originally_reachable && std::fabs(target_dist - dist) < 1e-3f;
  return out;
}

}  // namespace engine::animation
