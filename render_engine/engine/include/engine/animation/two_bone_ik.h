#pragma once

#include "engine/core/math.h"

namespace engine::animation {

// C11 / Mega-W8: analytic two-bone IK (law of cosines + pole plane).
struct TwoBoneIkInput {
  Vec3 root{};
  Vec3 mid{};
  Vec3 tip{};
  Vec3 target{};
  // Preferred bend plane: mid is pushed toward this point (not required to be unit).
  Vec3 pole{0.f, 1.f, 0.f};
};

struct TwoBoneIkOutput {
  Vec3 mid{};
  Vec3 tip{};
  bool reached = false;
};

// Places mid/tip so |tip-root| matches the chain reach toward target (clamped).
// Bone lengths are taken from the input rest positions (root→mid, mid→tip).
[[nodiscard]] TwoBoneIkOutput SolveTwoBoneIK(const TwoBoneIkInput& in);

}  // namespace engine::animation
