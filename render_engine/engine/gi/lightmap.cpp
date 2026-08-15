#include "engine/gi/lightmap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

namespace engine::gi {
namespace {

bool ParseMeta(const std::filesystem::path& meta_path, int& w, int& h) {
  std::ifstream in(meta_path);
  if (!in) {
    return false;
  }
  std::string line;
  w = 0;
  h = 0;
  while (std::getline(in, line)) {
    if (line.rfind("w=", 0) == 0) {
      w = std::atoi(line.c_str() + 2);
    } else if (line.rfind("h=", 0) == 0) {
      h = std::atoi(line.c_str() + 2);
    }
  }
  return w > 0 && h > 0;
}

int InferSize(std::size_t bytes) {
  if (bytes == 64ull * 64ull * 4ull) {
    return 64;
  }
  if (bytes == 128ull * 128ull * 4ull) {
    return 128;
  }
  const double side = std::sqrt(static_cast<double>(bytes / 4ull));
  const int s = static_cast<int>(side + 0.5);
  if (s > 0 && static_cast<std::size_t>(s) * static_cast<std::size_t>(s) * 4ull == bytes) {
    return s;
  }
  return 0;
}

}  // namespace

Status LoadLightmapRgba(const std::filesystem::path& rgba_path, LightmapImage& out) {
  std::ifstream in(rgba_path, std::ios::binary | std::ios::ate);
  if (!in) {
    return Status::Fail(ErrorCode::NotFound, "lightmap rgba not found: " + rgba_path.string());
  }
  const auto bytes = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  if (bytes == 0 || (bytes % 4) != 0) {
    return Status::Fail("lightmap rgba size invalid");
  }

  int w = 0;
  int h = 0;
  const auto meta_path = rgba_path.parent_path() / "lightmap.meta.txt";
  if (!ParseMeta(meta_path, w, h)) {
    const int side = InferSize(bytes);
    if (side <= 0) {
      return Status::Fail("lightmap meta missing and size not square rgba8");
    }
    w = side;
    h = side;
  }
  if (static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4ull != bytes) {
    return Status::Fail("lightmap meta size mismatch vs rgba bytes");
  }

  out.width = w;
  out.height = h;
  out.rgba.resize(bytes);
  in.read(reinterpret_cast<char*>(out.rgba.data()), static_cast<std::streamsize>(bytes));
  if (!in) {
    return Status::Fail("lightmap rgba read failed");
  }
  return Status::Ok();
}

ColorRgba SampleLightmap(const LightmapImage& img, float u, float v) {
  if (img.width <= 0 || img.height <= 0 || img.rgba.size() < 4) {
    return {1.f, 1.f, 1.f, 1.f};
  }
  auto wrap01 = [](float x) {
    x = x - std::floor(x);
    return x < 0.f ? x + 1.f : x;
  };
  u = wrap01(u);
  v = wrap01(v);
  const float x = u * static_cast<float>(img.width) - 0.5f;
  const float y = v * static_cast<float>(img.height) - 0.5f;
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  auto at = [&](int ix, int iy) {
    ix = ((ix % img.width) + img.width) % img.width;
    iy = ((iy % img.height) + img.height) % img.height;
    const std::size_t i =
        static_cast<std::size_t>((iy * img.width + ix) * 4);
    return ColorRgba{img.rgba[i] / 255.f, img.rgba[i + 1] / 255.f, img.rgba[i + 2] / 255.f,
                     img.rgba[i + 3] / 255.f};
  };
  const ColorRgba c00 = at(x0, y0);
  const ColorRgba c10 = at(x0 + 1, y0);
  const ColorRgba c01 = at(x0, y0 + 1);
  const ColorRgba c11 = at(x0 + 1, y0 + 1);
  auto lerp = [](const ColorRgba& a, const ColorRgba& b, float t) {
    return ColorRgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                     a.a + (b.a - a.a) * t};
  };
  return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
}

void MultiplyAlbedoByLightmap(std::vector<std::uint8_t>& albedo_rgba, int albedo_w, int albedo_h,
                              const LightmapImage& lightmap) {
  if (albedo_w <= 0 || albedo_h <= 0 ||
      albedo_rgba.size() < static_cast<std::size_t>(albedo_w * albedo_h * 4)) {
    return;
  }
  for (int y = 0; y < albedo_h; ++y) {
    for (int x = 0; x < albedo_w; ++x) {
      const float u = (x + 0.5f) / static_cast<float>(albedo_w);
      const float v = (y + 0.5f) / static_cast<float>(albedo_h);
      const ColorRgba lm = SampleLightmap(lightmap, u, v);
      const std::size_t i = static_cast<std::size_t>((y * albedo_w + x) * 4);
      albedo_rgba[i + 0] = static_cast<std::uint8_t>(
          std::clamp(albedo_rgba[i + 0] / 255.f * lm.r, 0.f, 1.f) * 255.f + 0.5f);
      albedo_rgba[i + 1] = static_cast<std::uint8_t>(
          std::clamp(albedo_rgba[i + 1] / 255.f * lm.g, 0.f, 1.f) * 255.f + 0.5f);
      albedo_rgba[i + 2] = static_cast<std::uint8_t>(
          std::clamp(albedo_rgba[i + 2] / 255.f * lm.b, 0.f, 1.f) * 255.f + 0.5f);
    }
  }
}

}  // namespace engine::gi
