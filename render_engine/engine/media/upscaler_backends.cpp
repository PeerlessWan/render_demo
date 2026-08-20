#include "engine/media/upscaler_backends.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ADR 0044: unfreeze hooks. CreateUpscaler never returns a vendor name without a live
// Upscale() that calls FFX/NGX. Until GPU-bound dispatch is wired under these macros,
// factories return nullptr → builtin_bilinear (honest).

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

}  // namespace

std::unique_ptr<IUpscaler> TryCreateDlssUpscaler() {
  const bool dll = ProbeDll("nvngx_dlss.dll") || ProbeDll("nvngx.dll") || ProbeDll("_nvngx.dll");
#if defined(ENGINE_HAS_NGX_HEADERS)
  SetFeatureOverride("dlss", dll);
  static bool once = false;
  if (!once) {
    once = true;
    if (dll) {
      LogWarn("TryCreateDlssUpscaler: NGX headers+DLL present but GPU NGX Upscale not bound yet "
              "→ nullptr (ADR 0044; CreateUpscaler → FSR2/bilinear)");
    } else {
      LogWarn("TryCreateDlssUpscaler: NGX headers built-in, runtime DLL absent → nullptr");
    }
  }
  return nullptr;
#elif defined(ENGINE_WITH_NGX) && ENGINE_WITH_NGX
  SetFeatureOverride("dlss", false);
  static bool once = false;
  if (!once) {
    once = true;
    LogWarn("TryCreateDlssUpscaler: ENGINE_WITH_NGX=1 but nvsdk_ngx.h missing → nullptr (ADR 0044)");
  }
  (void)dll;
  return nullptr;
#else
  (void)dll;
  return nullptr;
#endif
}

std::unique_ptr<IUpscaler> TryCreateFsr2Upscaler() {
#if defined(ENGINE_HAS_FFX_FSR2_HEADERS)
  SetFeatureOverride("fsr2", true);
  static bool once = false;
  if (!once) {
    once = true;
    LogWarn("TryCreateFsr2Upscaler: FFX headers present but GPU FFX context not bound yet "
            "→ nullptr (ADR 0044; CreateUpscaler → builtin_bilinear)");
  }
  return nullptr;
#elif defined(ENGINE_WITH_FIDELITYFX) && ENGINE_WITH_FIDELITYFX
  SetFeatureOverride("fsr2", false);
  static bool once = false;
  if (!once) {
    once = true;
    LogWarn(
        "TryCreateFsr2Upscaler: ENGINE_WITH_FIDELITYFX=1 but ffx_fsr2.h missing → nullptr (ADR 0044)");
  }
  return nullptr;
#else
  return nullptr;
#endif
}

}  // namespace engine::media
