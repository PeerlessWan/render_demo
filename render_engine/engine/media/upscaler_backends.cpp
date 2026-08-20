#include "engine/media/upscaler_backends.h"

#include "engine/core/log.h"

// W16 ADR 0040: no fake Fsr2/Dlss classes that call bilinear under false names.
// Real FFX/NGX dispatch lands only when SDKs are vendored and Upscale() calls them.
// Until then TryCreate* returns nullptr so CreateUpscaler() stays on builtin_bilinear.

namespace engine::media {

std::unique_ptr<IUpscaler> TryCreateDlssUpscaler() {
#if defined(ENGINE_WITH_NGX) && ENGINE_WITH_NGX
  LogWarn("TryCreateDlssUpscaler: ENGINE_WITH_NGX set but NGX Upscale dispatch not wired; "
          "skip (ADR 0040 — no bilinear under name=dlss)");
  return nullptr;
#else
  return nullptr;
#endif
}

std::unique_ptr<IUpscaler> TryCreateFsr2Upscaler() {
#if defined(ENGINE_WITH_FIDELITYFX) && ENGINE_WITH_FIDELITYFX
  LogWarn("TryCreateFsr2Upscaler: ENGINE_WITH_FIDELITYFX set but FFX Upscale dispatch not wired; "
          "skip (ADR 0040 — no bilinear under name=fsr2)");
  return nullptr;
#else
  return nullptr;
#endif
}

}  // namespace engine::media
