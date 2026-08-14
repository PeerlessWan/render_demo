#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::gi {

struct SceneCaptureOrb {
  Vec3 position{};
  ColorRgba color{1, 1, 1, 1};
  float radius = 0.5f;
};

// Paint bright orbs into cubemap faces (approximate dynamic scene reflection without full GPU re-render).
void CaptureApproximateSceneFaces(std::vector<std::uint8_t>& rgba_faces, int face_size,
                                  const Vec3& probe_pos, std::span<const SceneCaptureOrb> orbs,
                                  const Vec3& sun_dir, const ColorRgba& sun_color, float sun_intensity,
                                  const ColorRgba& ambient);

}  // namespace engine::gi
