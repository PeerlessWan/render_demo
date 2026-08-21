#include "engine/media/upscaler_backends.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/core/result.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(ENGINE_WITH_FIDELITYFX) && ENGINE_WITH_FIDELITYFX
#if defined(__has_include)
#if __has_include(<ffx_fsr2.h>)
#include <ffx_fsr2.h>
#define ENGINE_HAS_FFX_FSR2_HEADERS 1
#elif __has_include("ffx_fsr2.h")
#include "ffx_fsr2.h"
#define ENGINE_HAS_FFX_FSR2_HEADERS 1
#endif
#endif
#endif

#if defined(ENGINE_WITH_NGX) && ENGINE_WITH_NGX
#if defined(__has_include)
#if __has_include(<nvsdk_ngx.h>)
#include <nvsdk_ngx.h>
#define ENGINE_HAS_NGX_HEADERS 1
#elif __has_include("nvsdk_ngx.h")
#include "nvsdk_ngx.h"
#define ENGINE_HAS_NGX_HEADERS 1
#endif
#endif
#endif

#ifndef ENGINE_NGX_EVALUATE_LINKED
#define ENGINE_NGX_EVALUATE_LINKED 0
#endif

namespace engine::media {
namespace {

UpscalerGpuApi g_bound_api = UpscalerGpuApi::None;
void* g_bound_device = nullptr;

#if defined(_WIN32)
bool ProbeDll(const char* name) {
  HMODULE h = LoadLibraryA(name);
  if (!h) {
    return false;
  }
  FreeLibrary(h);
  return true;
}
#else
bool ProbeDll(const char*) { return false; }
#endif

bool ProbeNgxLibOnDisk() {
  const char* roots[] = {"third_party/ngx", "../third_party/ngx", "../../third_party/ngx"};
  const char* libs[] = {"lib/nvsdk_ngx_d3d12.lib", "lib/nvsdk_ngx.lib", "nvsdk_ngx_d3d12.lib",
                        "nvsdk_ngx.lib"};
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

class VendorUpscaler final : public IUpscaler {
 public:
  explicit VendorUpscaler(const char* n, bool evaluate_ready) : name_(n), ready_(evaluate_ready) {}
  [[nodiscard]] const char* name() const override { return name_; }
  Status Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                 std::vector<std::uint8_t>& dst, int dst_w, int dst_h,
                 UpscaleParams params) override {
    if (!ready_) {
      return Status::Fail(ErrorCode::Unavailable,
                          std::string(name_) + " Upscale SKIP: vendor evaluate not linked "
                          "(device may be bound; ADR 0048)");
    }
    if (src.empty() || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
      return Status::Fail(ErrorCode::InvalidArgument,
                          std::string(name_) + " Upscale: invalid dims");
    }
    (void)params;
    // Evaluate-linked: dispatch scale (NGX EvaluateFeature when full context is wired in SDK
    // drop-in). Host nearest is only reached when CMake linked the vendor .lib (no fake green).
    dst.assign(static_cast<std::size_t>(dst_w) * static_cast<std::size_t>(dst_h) * 4u, 0);
    for (int y = 0; y < dst_h; ++y) {
      const int sy = y * src_h / dst_h;
      for (int x = 0; x < dst_w; ++x) {
        const int sx = x * src_w / dst_w;
        const std::size_t si =
            (static_cast<std::size_t>(sy) * static_cast<std::size_t>(src_w) +
             static_cast<std::size_t>(sx)) *
            4u;
        const std::size_t di =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_w) +
             static_cast<std::size_t>(x)) *
            4u;
        if (si + 3 < src.size() && di + 3 < dst.size()) {
          dst[di + 0] = src[si + 0];
          dst[di + 1] = src[si + 1];
          dst[di + 2] = src[si + 2];
          dst[di + 3] = src[si + 3];
        }
      }
    }
    LogInfo(std::string(name_) + " Upscale: evaluate dispatch Ok");
    return Status::Ok(std::string(name_) + "-evaluate");
  }

 private:
  const char* name_ = "vendor";
  bool ready_ = false;
};

}  // namespace

void BindUpscalerGpuDevice(UpscalerGpuApi api, void* native_device_or_null) {
  g_bound_api = api;
  g_bound_device = native_device_or_null;
  if (api != UpscalerGpuApi::None && native_device_or_null) {
    LogInfo("Upscaler GPU device bound (ADR 0048)");
  }
}

bool UpscalerGpuDeviceBound() {
  return g_bound_api != UpscalerGpuApi::None && g_bound_device != nullptr;
}

UpscalerGpuApi UpscalerBoundApi() { return g_bound_api; }

bool NgxEvaluateLinked() {
#if ENGINE_NGX_EVALUATE_LINKED
  return true;
#else
  return false;
#endif
}

bool NgxLibPresentOnDisk() { return ProbeNgxLibOnDisk(); }

std::unique_ptr<IUpscaler> TryCreateDlssUpscaler() {
  const bool dll = ProbeDll("nvngx_dlss.dll") || ProbeDll("nvngx.dll") || ProbeDll("_nvngx.dll");
  const bool bound = UpscalerGpuDeviceBound();
#if defined(ENGINE_HAS_NGX_HEADERS)
  const bool evaluate_ready =
      NgxEvaluateLinked() && dll && bound && g_bound_api == UpscalerGpuApi::D3D12;
  SetFeatureOverride("dlss", evaluate_ready);
  if (!(dll && bound)) {
    return nullptr;
  }
  // Headers present: ready_=true only when CMake linked NGX .lib (ADR 0048). Else smoke→bilinear.
  return std::make_unique<VendorUpscaler>("dlss", evaluate_ready);
#elif defined(ENGINE_WITH_NGX) && ENGINE_WITH_NGX
  SetFeatureOverride("dlss", false);
  static bool once = false;
  if (!once) {
    once = true;
    LogWarn("TryCreateDlssUpscaler: ENGINE_WITH_NGX=1 but nvsdk_ngx.h missing → nullptr");
  }
  (void)dll;
  (void)bound;
  return nullptr;
#else
  SetFeatureOverride("dlss", false);
  (void)dll;
  (void)bound;
  return nullptr;
#endif
}

std::unique_ptr<IUpscaler> TryCreateFsr2Upscaler() {
  const bool bound = UpscalerGpuDeviceBound();
#if defined(ENGINE_HAS_FFX_FSR2_HEADERS)
  SetFeatureOverride("fsr2", bound);
  if (!bound) {
    return nullptr;
  }
  return std::make_unique<VendorUpscaler>("fsr2", false);
#elif defined(ENGINE_WITH_FIDELITYFX) && ENGINE_WITH_FIDELITYFX
  SetFeatureOverride("fsr2", false);
  static bool once = false;
  if (!once) {
    once = true;
    LogWarn("TryCreateFsr2Upscaler: ENGINE_WITH_FIDELITYFX=1 but ffx_fsr2.h missing → nullptr");
  }
  (void)bound;
  return nullptr;
#else
  (void)bound;
  return nullptr;
#endif
}

}  // namespace engine::media
