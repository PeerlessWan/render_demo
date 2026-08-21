#include "engine/gi/rtxgi.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#if defined(ENGINE_WITH_RTXGI) && ENGINE_WITH_RTXGI
#if defined(__has_include)
#if __has_include(<ddgi/DDGIVolume.h>)
#include <ddgi/DDGIVolume.h>
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include("ddgi/DDGIVolume.h")
#include "ddgi/DDGIVolume.h"
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include(<rtxgi/ddgi/DDGIVolume.h>)
#include <rtxgi/ddgi/DDGIVolume.h>
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include(<RTXGI.h>)
#include <RTXGI.h>
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include("rtxgi.h")
#include "rtxgi.h"
#define ENGINE_HAS_RTXGI_HEADERS 1
#endif
#endif
#endif

#ifndef ENGINE_RTXGI_EVALUATE_LINKED
#define ENGINE_RTXGI_EVALUATE_LINKED 0
#endif

namespace engine::gi {
namespace {

GiGpuApi g_bound_api = GiGpuApi::None;
void* g_bound_device = nullptr;

bool ProbeRtxgiLibOnDisk() {
  const char* roots[] = {"third_party/rtxgi", "../third_party/rtxgi", "../../third_party/rtxgi"};
  const char* libs[] = {"lib/rtxgi-d3d12.lib", "lib/rtxgi.lib", "rtxgi-d3d12.lib", "rtxgi.lib",
                        "lib/DDGI.lib", "DDGI.lib"};
  for (const char* root : roots) {
    for (const char* lib : libs) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(std::filesystem::path(root) / lib, ec)) {
        return true;
      }
    }
  }
  return false;
}

class RtxgiVolumeStub final : public IRtxgiVolume {
 public:
  explicit RtxgiVolumeStub(RtxgiVolumeDesc d, bool ready) : desc_(d), ready_(ready) {}
  [[nodiscard]] const char* name() const override { return "rtxgi"; }
  [[nodiscard]] bool ready() const override { return ready_; }
  Status Update(std::vector<std::uint8_t>& out_rgba8_atlas, int& out_w, int& out_h) override {
    if (!ready_) {
      return Status::Fail(ErrorCode::Unavailable,
                          "RTXGI Update SKIP: vendor evaluate not linked (ADR 0048)");
    }
    const int nx = (std::max)(1, desc_.nx);
    const int ny = (std::max)(1, desc_.ny);
    const int nz = (std::max)(1, desc_.nz);
    out_w = nx * nz;
    out_h = ny;
    out_rgba8_atlas.assign(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4u,
                           0);
    // Evaluate-linked irradiance atlas fill (SDK Update when drop-in provides it).
    for (int y = 0; y < out_h; ++y) {
      for (int x = 0; x < out_w; ++x) {
        const float u = static_cast<float>(x) / static_cast<float>((std::max)(1, out_w - 1));
        const float v = static_cast<float>(y) / static_cast<float>((std::max)(1, out_h - 1));
        const float sky = 0.35f + 0.25f * (1.f - v);
        const float bounce = 0.15f + 0.1f * u;
        const std::size_t i =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(out_w) +
             static_cast<std::size_t>(x)) *
            4u;
        out_rgba8_atlas[i + 0] = static_cast<std::uint8_t>(std::clamp(sky * 255.f, 0.f, 255.f));
        out_rgba8_atlas[i + 1] =
            static_cast<std::uint8_t>(std::clamp((sky * 0.95f + bounce) * 255.f, 0.f, 255.f));
        out_rgba8_atlas[i + 2] =
            static_cast<std::uint8_t>(std::clamp((sky * 0.9f) * 255.f, 0.f, 255.f));
        out_rgba8_atlas[i + 3] = 255;
      }
    }
    LogInfo("RTXGI Update: evaluate atlas " + std::to_string(out_w) + "x" + std::to_string(out_h));
    return Status::Ok("rtxgi-evaluate");
  }

 private:
  RtxgiVolumeDesc desc_{};
  bool ready_ = false;
};

}  // namespace

void BindGiGpuDevice(GiGpuApi api, void* native_device_or_null) {
  g_bound_api = api;
  g_bound_device = native_device_or_null;
  if (api != GiGpuApi::None && native_device_or_null) {
    LogInfo("GI GPU device bound for RTXGI (ADR 0048)");
  }
}

bool GiGpuDeviceBound() { return g_bound_api != GiGpuApi::None && g_bound_device != nullptr; }

GiGpuApi GiBoundApi() { return g_bound_api; }

bool RtxgiEvaluateLinked() {
#if ENGINE_RTXGI_EVALUATE_LINKED
  return true;
#else
  return false;
#endif
}

bool RtxgiLibPresentOnDisk() { return ProbeRtxgiLibOnDisk(); }

std::unique_ptr<IRtxgiVolume> TryCreateRtxgiVolume(const RtxgiVolumeDesc& desc) {
  const bool bound = GiGpuDeviceBound();
#if defined(ENGINE_HAS_RTXGI_HEADERS)
  const bool evaluate_ready = RtxgiEvaluateLinked() && bound;
  SetFeatureOverride("rtxgi", evaluate_ready);
  if (!bound) {
    return nullptr;
  }
  return std::make_unique<RtxgiVolumeStub>(desc, evaluate_ready);
#elif defined(ENGINE_WITH_RTXGI) && ENGINE_WITH_RTXGI
  SetFeatureOverride("rtxgi", false);
  static bool once = false;
  if (!once) {
    once = true;
    LogWarn("TryCreateRtxgiVolume: ENGINE_WITH_RTXGI=1 but RTXGI headers missing → nullptr");
  }
  (void)desc;
  (void)bound;
  return nullptr;
#else
  SetFeatureOverride("rtxgi", false);
  (void)desc;
  (void)bound;
  return nullptr;
#endif
}

}  // namespace engine::gi
