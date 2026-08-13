#include "engine/assets/texture_loader.h"

#include <algorithm>
#include <fstream>
#include <string>

#if defined(ENGINE_WITH_DDSKTX) && ENGINE_WITH_DDSKTX
#define DDSKTX_IMPLEMENT
#include "dds-ktx.h"
#endif

namespace engine::assets {
namespace {

class FallbackTextureLoader final : public ITextureLoader {
 public:
  explicit FallbackTextureLoader(std::unique_ptr<IImageLoader> images)
      : images_(std::move(images)) {}

  Result<TextureImage> LoadFile(const std::filesystem::path& path) const override {
    auto img = images_->LoadFile(path);
    if (!img) {
      return Result<TextureImage>::Fail(img.status());
    }
    TextureImage out;
    out.w = img->width;
    out.h = img->height;
    out.format = TextureFormat::Rgba8;
    out.bytes = std::move(img->rgba);
    return Result<TextureImage>::Ok(std::move(out));
  }

  Result<TextureImage> LoadMemory(std::span<const std::uint8_t> bytes) const override {
    auto img = images_->LoadMemory(bytes);
    if (!img) {
      return Result<TextureImage>::Fail(img.status());
    }
    TextureImage out;
    out.w = img->width;
    out.h = img->height;
    out.format = TextureFormat::Rgba8;
    out.bytes = std::move(img->rgba);
    return Result<TextureImage>::Ok(std::move(out));
  }

  const char* backend_name() const override { return "image_fallback"; }

 private:
  std::unique_ptr<IImageLoader> images_;
};

#if defined(ENGINE_WITH_DDSKTX) && ENGINE_WITH_DDSKTX

bool EndsWithIgnoreCase(const std::filesystem::path& path, const char* ext) {
  auto s = path.extension().string();
  for (auto& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s == ext;
}

TextureFormat MapFormat(ddsktx_format fmt) {
  switch (fmt) {
    case DDSKTX_FORMAT_RGBA8:
    case DDSKTX_FORMAT_BGRA8:
      return TextureFormat::Rgba8;
    case DDSKTX_FORMAT_BC1:
      return TextureFormat::Bc1;
    case DDSKTX_FORMAT_BC3:
      return TextureFormat::Bc3;
    case DDSKTX_FORMAT_BC7:
      return TextureFormat::Bc7;
    default:
      return TextureFormat::Unknown;
  }
}

Result<TextureImage> DecodeDdsMemory(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return Result<TextureImage>::Fail("Empty DDS bytes");
  }
  ddsktx_texture_info info{};
  ddsktx_error err{};
  if (!ddsktx_parse(&info, bytes.data(), static_cast<int>(bytes.size()), &err)) {
    return Result<TextureImage>::Fail(std::string("dds-ktx parse failed: ") + err.msg);
  }
  if (info.width <= 0 || info.height <= 0) {
    return Result<TextureImage>::Fail("dds-ktx: invalid dimensions");
  }

  ddsktx_sub_data sub{};
  ddsktx_get_sub(&info, &sub, bytes.data(), static_cast<int>(bytes.size()), 0, 0, 0);
  if (!sub.buff || sub.size_bytes <= 0) {
    return Result<TextureImage>::Fail("dds-ktx: empty mip0");
  }

  TextureImage out;
  out.w = info.width;
  out.h = info.height;
  out.format = MapFormat(info.format);
  if (out.format == TextureFormat::Unknown) {
    return Result<TextureImage>::Fail(std::string("unsupported DDS format: ") +
                                      ddsktx_format_str(info.format));
  }

  const auto* src = static_cast<const std::uint8_t*>(sub.buff);
  if (info.format == DDSKTX_FORMAT_BGRA8) {
    out.bytes.resize(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h) * 4u);
    for (int y = 0; y < out.h; ++y) {
      const auto* row = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(sub.row_pitch_bytes);
      for (int x = 0; x < out.w; ++x) {
        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.w) +
                               static_cast<std::size_t>(x)) *
                              4u;
        out.bytes[i + 0] = row[x * 4 + 2];
        out.bytes[i + 1] = row[x * 4 + 1];
        out.bytes[i + 2] = row[x * 4 + 0];
        out.bytes[i + 3] = row[x * 4 + 3];
      }
    }
  } else if (info.format == DDSKTX_FORMAT_RGBA8) {
    out.bytes.assign(src, src + static_cast<std::size_t>(sub.size_bytes));
    // If row pitch > width*4, pack tightly.
    const int packed_pitch = out.w * 4;
    if (sub.row_pitch_bytes > packed_pitch) {
      out.bytes.resize(static_cast<std::size_t>(packed_pitch) * static_cast<std::size_t>(out.h));
      for (int y = 0; y < out.h; ++y) {
        const auto* row =
            src + static_cast<std::size_t>(y) * static_cast<std::size_t>(sub.row_pitch_bytes);
        std::copy(row, row + packed_pitch,
                  out.bytes.begin() + static_cast<std::size_t>(y) * static_cast<std::size_t>(packed_pitch));
      }
    }
  } else {
    // BC1/BC3/BC7: keep compressed block bytes as-is.
    out.bytes.assign(src, src + static_cast<std::size_t>(sub.size_bytes));
  }
  return Result<TextureImage>::Ok(std::move(out));
}

class DdsTextureLoader final : public ITextureLoader {
 public:
  explicit DdsTextureLoader(std::unique_ptr<IImageLoader> images)
      : fallback_(std::move(images)) {}

  Result<TextureImage> LoadFile(const std::filesystem::path& path) const override {
    if (EndsWithIgnoreCase(path, ".dds") || EndsWithIgnoreCase(path, ".ktx")) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return Result<TextureImage>::Fail("Cannot open texture: " + path.string());
      }
      in.seekg(0, std::ios::end);
      const auto size = static_cast<std::streamoff>(in.tellg());
      in.seekg(0, std::ios::beg);
      if (size <= 0) {
        return Result<TextureImage>::Fail("Empty texture file: " + path.string());
      }
      std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
      in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      return DecodeDdsMemory(bytes);
    }
    return fallback_.LoadFile(path);
  }

  Result<TextureImage> LoadMemory(std::span<const std::uint8_t> bytes) const override {
    // Prefer DDS/KTX magic when present.
    if (bytes.size() >= 4) {
      const bool dds = bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ';
      const bool ktx = bytes.size() >= 12 && bytes[0] == 0xAB && bytes[1] == 'K' && bytes[2] == 'T' &&
                       bytes[3] == 'X';
      if (dds || ktx) {
        return DecodeDdsMemory(bytes);
      }
    }
    return fallback_.LoadMemory(bytes);
  }

  const char* backend_name() const override { return "dds-ktx+image"; }

 private:
  FallbackTextureLoader fallback_;
};

#endif  // ENGINE_WITH_DDSKTX

}  // namespace

std::unique_ptr<ITextureLoader> CreateDefaultTextureLoader() {
  auto images = CreateDefaultImageLoader();
#if defined(ENGINE_WITH_DDSKTX) && ENGINE_WITH_DDSKTX
  return std::make_unique<DdsTextureLoader>(std::move(images));
#else
  return std::make_unique<FallbackTextureLoader>(std::move(images));
#endif
}

}  // namespace engine::assets
