#pragma once

#include "engine/assets/image_loader.h"
#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace engine::assets {

enum class TextureFormat {
  Rgba8,
  Bc1,
  Bc3,
  Bc7,
  Unknown,
};

struct TextureImage {
  int w = 0;
  int h = 0;
  TextureFormat format = TextureFormat::Unknown;
  std::vector<std::uint8_t> bytes;
};

class ITextureLoader {
 public:
  virtual ~ITextureLoader() = default;
  virtual Result<TextureImage> LoadFile(const std::filesystem::path& path) const = 0;
  virtual Result<TextureImage> LoadMemory(std::span<const std::uint8_t> bytes) const = 0;
  [[nodiscard]] virtual const char* backend_name() const = 0;
};

// DDS via dds-ktx when ENGINE_WITH_DDSKTX=1; PNG/JPEG fall back to IImageLoader.
std::unique_ptr<ITextureLoader> CreateDefaultTextureLoader();

}  // namespace engine::assets
