#include "engine/media/upscaler.h"

#include <algorithm>
#include <cmath>

namespace engine::media {
namespace {

float SampleChannel(std::span<const std::uint8_t> src, int w, int h, int c, float u, float v) {
  const float x = std::clamp(u * static_cast<float>(w - 1), 0.f, static_cast<float>(w - 1));
  const float y = std::clamp(v * static_cast<float>(h - 1), 0.f, static_cast<float>(h - 1));
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, w - 1);
  const int y1 = std::min(y0 + 1, h - 1);
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  const auto at = [&](int px, int py) {
    return static_cast<float>(src[static_cast<std::size_t>((py * w + px) * 4 + c)]);
  };
  const float v00 = at(x0, y0);
  const float v10 = at(x1, y0);
  const float v01 = at(x0, y1);
  const float v11 = at(x1, y1);
  const float hx0 = v00 + (v10 - v00) * fx;
  const float hx1 = v01 + (v11 - v01) * fx;
  return hx0 + (hx1 - hx0) * fy;
}

}  // namespace

Status BuiltinBilinearUpscaler::Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                                        std::vector<std::uint8_t>& dst, int dst_w, int dst_h) {
  if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    return Status::Fail("invalid upscale dimensions");
  }
  if (src_w == dst_w && src_h == dst_h) {
    dst.assign(src.begin(), src.end());
    return Status::Ok();
  }
  if (static_cast<int>(src.size()) < src_w * src_h * 4) {
    return Status::Fail("source buffer too small");
  }
  dst.resize(static_cast<std::size_t>(dst_w * dst_h * 4));
  for (int y = 0; y < dst_h; ++y) {
    for (int x = 0; x < dst_w; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(dst_w - 1);
      const float v = static_cast<float>(y) / static_cast<float>(dst_h - 1);
      const std::size_t o = static_cast<std::size_t>((y * dst_w + x) * 4);
      for (int c = 0; c < 4; ++c) {
        dst[o + static_cast<std::size_t>(c)] =
            static_cast<std::uint8_t>(SampleChannel(src, src_w, src_h, c, u, v) + 0.5f);
      }
    }
  }
  return Status::Ok();
}

std::unique_ptr<IUpscaler> CreateUpscaler() { return std::make_unique<BuiltinBilinearUpscaler>(); }

}  // namespace engine::media
