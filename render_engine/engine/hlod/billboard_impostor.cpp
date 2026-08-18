#include "engine/hlod/billboard_impostor.h"

#include <algorithm>
#include <cstdio>

namespace engine::hlod {

LodMode BillboardImpostor::SwitchLod(float distance) const {
  if (distance < distance_threshold) {
    return LodMode::NearMesh;
  }
  return LodMode::Impostor;
}

std::string BakeImpostorPlaceholder(const ColorRgba& color) {
  // Deterministic id from quantized RGBA (8-bit) for test equality.
  char buf[96];
  const int r = static_cast<int>(std::clamp(color.r, 0.f, 1.f) * 255.f + 0.5f);
  const int g = static_cast<int>(std::clamp(color.g, 0.f, 1.f) * 255.f + 0.5f);
  const int b = static_cast<int>(std::clamp(color.b, 0.f, 1.f) * 255.f + 0.5f);
  const int a = static_cast<int>(std::clamp(color.a, 0.f, 1.f) * 255.f + 0.5f);
  std::snprintf(buf, sizeof(buf), "impostor/solid_%02x%02x%02x%02x", r, g, b, a);
  return std::string(buf);
}

}  // namespace engine::hlod
