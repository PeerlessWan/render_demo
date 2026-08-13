#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace engine::assets {

struct ImageRgba8 {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;  // width*height*4, row-major
};

class IImageLoader {
 public:
  virtual ~IImageLoader() = default;
  virtual Result<ImageRgba8> LoadFile(const std::filesystem::path& path) const = 0;
  virtual Result<ImageRgba8> LoadMemory(std::span<const std::uint8_t> bytes) const = 0;
  [[nodiscard]] virtual const char* backend_name() const = 0;
};

// Default: stb_image when ENGINE_WITH_STB_IMAGE=1; otherwise Fail.
std::unique_ptr<IImageLoader> CreateDefaultImageLoader();

}  // namespace engine::assets
