#pragma once

#include "engine/core/math.h"

namespace engine::render {

// C03 / W7: analytic IES-like spot intensity (not a full IES file ecosystem).
// profile: 0 = off (1), 1 = narrow beam, 2 = wide wash, 3 = batwing.
[[nodiscard]] float EvalIesFactor(float cos_theta, int profile);

// 1D LUT stand-in: sample profile curve at u in [0,1] (0 = along axis, 1 = 90°).
[[nodiscard]] float SampleIesLut(float u, int profile);

}  // namespace engine::render
