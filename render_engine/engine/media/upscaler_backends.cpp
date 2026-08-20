#include "engine/media/upscaler_backends.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/core/result.h"

#include <cstdint>
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

// Vendor-backed upscaler: Upscale succeeds only when evaluate is linked.
// Until then CreateUpscaler smoke-tests and falls through to bilinear (honest).
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
                          "(device may be bound; ADR 0045)");
    }
    // Evaluate path would call NGX/FFX here. Host fallback is forbidden under vendor name.
    (void)src;
    (void)src_w;
    (void)src_h;
    (void)dst;
    (void)dst_w;
    (void)dst_h;
    (void)params;
    return Status::Fail(ErrorCode::Unavailable,
                        std::string(name_) + " Upscale SKIP: evaluate entry not implemented");
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
    LogInfo("Upscaler GPU device bound (ADR 0045)");
  }
}

bool UpscalerGpuDeviceBound() {
  return g_bound_api != UpscalerGpuApi::None && g_bound_device != nullptr;
}

UpscalerGpuApi UpscalerBoundApi() { return g_bound_api; }

std::unique_ptr<IUpscaler> TryCreateDlssUpscaler() {
  const bool dll = ProbeDll("nvngx_dlss.dll") || ProbeDll("nvngx.dll") || ProbeDll("_nvngx.dll");
  const bool bound = UpscalerGpuDeviceBound();
#if defined(ENGINE_HAS_NGX_HEADERS)
  SetFeatureOverride("dlss", dll && bound);
  if (!(dll && bound)) {
    return nullptr;
  }
  // Headers present: still need NGX evaluate link. ready_=false → CreateUpscaler smoke fail → bilinear.
  return std::make_unique<VendorUpscaler>("dlss", false);
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
