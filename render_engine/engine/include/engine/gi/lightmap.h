#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::gi {

// Runtime image for tools/lightmap_baker output:
//   lightmap.rgba  — raw RGBA8 row-major
//   lightmap.meta.txt — w=N h=N format=rgba8
struct LightmapImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

// Load baker RGBA (+ optional sibling .meta.txt). Infers 64²/128² from byte size if meta missing.
Status LoadLightmapRgba(const std::filesystem::path& rgba_path, LightmapImage& out);

// Bilinear sample in [0,1] UV (wraps).
[[nodiscard]] ColorRgba SampleLightmap(const LightmapImage& img, float u, float v);

// In-place albedo RGB *= lightmap sample (keeps alpha). Sizes may differ (UV remap).
void MultiplyAlbedoByLightmap(std::vector<std::uint8_t>& albedo_rgba, int albedo_w, int albedo_h,
                              const LightmapImage& lightmap);

}  // namespace engine::gi
