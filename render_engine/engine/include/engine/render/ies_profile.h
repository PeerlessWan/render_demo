#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// C03 / W7–W8: analytic IES-like profiles + optional file LUT.

struct IesLut {
  // Intensity samples along u in [0,1] (0 = along axis / 0°, 1 = 90°+).
  std::vector<float> samples;
  float max_candela = 1.f;
};

// profile: 0 = off (1), 1 = narrow beam, 2 = wide wash, 3 = batwing.
// profile >= 100: Sample registered LUT (RegisterIesLut).
[[nodiscard]] float EvalIesFactor(float cos_theta, int profile);

// 1D LUT stand-in: sample profile curve at u in [0,1] (0 = along axis, 1 = 90°).
[[nodiscard]] float SampleIesLut(float u, int profile);

// Mega-W8: evaluate a loaded LUT directly.
[[nodiscard]] float SampleIesLut(float u, const IesLut& lut);
[[nodiscard]] float EvalIesFactor(float cos_theta, const IesLut& lut);

// Parse a minimal IESNA LM-63 text blob → vertical intensity LUT (horizontally averaged).
[[nodiscard]] Result<IesLut> LoadIesText(std::string_view text);
[[nodiscard]] Result<IesLut> LoadIesFile(const std::filesystem::path& path);

// Register LUT; returns profile id (>= 100) usable with EvalIesFactor(cos, id).
[[nodiscard]] int RegisterIesLut(IesLut lut);
void ClearRegisteredIesLuts();

}  // namespace engine::render
