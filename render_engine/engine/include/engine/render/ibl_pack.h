#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace engine::render {

// Packed IBL from tools/ibl_baker (magic IBL1).
struct IblPack {
  int face_size = 0;
  int lut_w = 0;
  int lut_h = 0;
  std::vector<std::uint8_t> irradiance_rgba;  // 6 * face^2 * 4
  std::vector<std::uint8_t> prefilter_rgba;
  std::vector<std::uint8_t> brdf_lut_rgba;
};

Result<IblPack> LoadIblPack(const std::filesystem::path& path);

}  // namespace engine::render
