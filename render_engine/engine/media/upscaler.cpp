#include "engine/media/upscaler.h"

#include "engine/media/upscaler_backends.h"

#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <vector>

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

bool EnvPrefers(std::string_view want) {
  const char* v = std::getenv("ENGINE_UPSCALER");
  return v && v[0] && std::string_view(v) == want;
}

}  // namespace

float ResolutionScale::ClampScale(float scale) {
  if (!(scale > 0.f)) {
    return 1.f;
  }
  return std::clamp(scale, 0.5f, 1.f);
}

void ResolutionScale::ComputeRenderSize(int display_w, int display_h, float scale, int& out_w,
                                        int& out_h) {
  const float s = ClampScale(scale);
  out_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(display_w) * s)));
  out_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(display_h) * s)));
  out_w = std::min(out_w, std::max(1, display_w));
  out_h = std::min(out_h, std::max(1, display_h));
}

Status BuiltinBilinearUpscaler::Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                                        std::vector<std::uint8_t>& dst, int dst_w, int dst_h,
                                        UpscaleParams params) {
  if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    return Status::Fail("invalid upscale dimensions");
  }
  if (src_w == dst_w && src_h == dst_h && params.jitter_x == 0.f && params.jitter_y == 0.f) {
    dst.assign(src.begin(), src.end());
    return Status::Ok();
  }
  if (static_cast<int>(src.size()) < src_w * src_h * 4) {
    return Status::Fail("source buffer too small");
  }
  dst.resize(static_cast<std::size_t>(dst_w * dst_h * 4));
  const float ju = params.jitter_x * 0.5f;
  const float jv = params.jitter_y * 0.5f;
  for (int y = 0; y < dst_h; ++y) {
    for (int x = 0; x < dst_w; ++x) {
      float u = static_cast<float>(x) / static_cast<float>(std::max(1, dst_w - 1));
      float v = static_cast<float>(y) / static_cast<float>(std::max(1, dst_h - 1));
      u = std::clamp(u + ju, 0.f, 1.f);
      v = std::clamp(v + jv, 0.f, 1.f);
      const std::size_t o = static_cast<std::size_t>((y * dst_w + x) * 4);
      for (int c = 0; c < 4; ++c) {
        dst[o + static_cast<std::size_t>(c)] =
            static_cast<std::uint8_t>(SampleChannel(src, src_w, src_h, c, u, v) + 0.5f);
      }
    }
  }
  return Status::Ok();
}

std::unique_ptr<IUpscaler> CreateUpscaler() {
  // W22 ADR 0045: DLSS → FSR2 → builtin. Smoke Upscale; discard vendor if SKIP (honest).
  // Intel XeSS not supported.
  auto smoke_ok = [](IUpscaler& u) {
    std::vector<std::uint8_t> src = {255, 0, 0, 255};
    std::vector<std::uint8_t> dst;
    return static_cast<bool>(u.Upscale(src, 1, 1, dst, 1, 1));
  };
  if (auto dlss = TryCreateDlssUpscaler()) {
    if (smoke_ok(*dlss)) {
      return dlss;
    }
    static bool logged = false;
    if (!logged) {
      logged = true;
      LogWarn("DLSS present but Upscale not ready → continue fallback (ADR 0045)");
    }
  } else if (EnvPrefers("dlss")) {
    static bool logged_dlss = false;
    if (!logged_dlss) {
      logged_dlss = true;
      LogWarn("DLSS SDK/device absent → continue fallback chain");
    }
  }
  if (auto fsr = TryCreateFsr2Upscaler()) {
    if (smoke_ok(*fsr)) {
      return fsr;
    }
    static bool logged_fsr = false;
    if (!logged_fsr) {
      logged_fsr = true;
      LogWarn("FSR2 present but Upscale not ready → builtin_bilinear (ADR 0045)");
    }
  } else if (EnvPrefers("fsr") || EnvPrefers("fsr2")) {
    static bool logged_fsr_absent = false;
    if (!logged_fsr_absent) {
      logged_fsr_absent = true;
      LogWarn("FSR2 SDK/device absent → builtin_bilinear");
    }
  }
  return std::make_unique<BuiltinBilinearUpscaler>();
}

}  // namespace engine::media
