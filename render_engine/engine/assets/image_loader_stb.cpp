#include "engine/assets/image_loader.h"

#include "engine/core/log.h"

#include <fstream>

#if defined(ENGINE_WITH_STB_IMAGE) && ENGINE_WITH_STB_IMAGE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#include "stb_image.h"
#endif

namespace engine::assets {
namespace {

class NullImageLoader final : public IImageLoader {
 public:
  Result<ImageRgba8> LoadFile(const std::filesystem::path&) const override {
    return Result<ImageRgba8>::Fail("Image loader disabled (ENGINE_WITH_STB_IMAGE=0)");
  }
  Result<ImageRgba8> LoadMemory(std::span<const std::uint8_t>) const override {
    return Result<ImageRgba8>::Fail("Image loader disabled (ENGINE_WITH_STB_IMAGE=0)");
  }
  const char* backend_name() const override { return "null"; }
};

#if defined(ENGINE_WITH_STB_IMAGE) && ENGINE_WITH_STB_IMAGE
class StbImageLoader final : public IImageLoader {
 public:
  Result<ImageRgba8> LoadFile(const std::filesystem::path& path) const override {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      return Result<ImageRgba8>::Fail("Cannot open image: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamoff>(in.tellg());
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
      return Result<ImageRgba8>::Fail("Empty image file: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return LoadMemory(bytes);
  }

  Result<ImageRgba8> LoadMemory(std::span<const std::uint8_t> bytes) const override {
    if (bytes.empty()) {
      return Result<ImageRgba8>::Fail("Empty image bytes");
    }
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h,
                                          &comp, 4);
    if (!data || w <= 0 || h <= 0) {
      const char* reason = stbi_failure_reason();
      return Result<ImageRgba8>::Fail(std::string("stb_image decode failed: ") +
                                      (reason ? reason : "unknown"));
    }
    ImageRgba8 out;
    out.width = w;
    out.height = h;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    out.rgba.assign(data, data + n);
    stbi_image_free(data);
    return Result<ImageRgba8>::Ok(std::move(out));
  }

  const char* backend_name() const override { return "stb_image"; }
};
#endif

}  // namespace

std::unique_ptr<IImageLoader> CreateDefaultImageLoader() {
#if defined(ENGINE_WITH_STB_IMAGE) && ENGINE_WITH_STB_IMAGE
  return std::make_unique<StbImageLoader>();
#else
  return std::make_unique<NullImageLoader>();
#endif
}

}  // namespace engine::assets
