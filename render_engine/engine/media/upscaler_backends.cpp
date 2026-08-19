#include "engine/media/upscaler_backends.h"

#include "engine/core/log.h"

#include <cstdlib>
#include <string_view>

#if defined(ENGINE_WITH_FIDELITYFX) && ENGINE_WITH_FIDELITYFX
// Real FidelityFX FSR2 integration lands when third_party SDK is vendored
// (tools/fetch_fidelityfx.ps1). Until linked, this TU still compiles the stub path.
#endif

#if defined(ENGINE_WITH_NGX) && ENGINE_WITH_NGX
// NVIDIA NGX DLSS when SDK present.
#endif

namespace engine::media {
namespace {

bool EnvEquals(const char* key, std::string_view want) {
  const char* v = std::getenv(key);
  return v && v[0] && std::string_view(v) == want;
}

#if defined(ENGINE_WITH_FIDELITYFX) && ENGINE_WITH_FIDELITYFX
class Fsr2Upscaler final : public IUpscaler {
 public:
  [[nodiscard]] const char* name() const override { return "fsr2"; }
  Status Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                 std::vector<std::uint8_t>& dst, int dst_w, int dst_h,
                 UpscaleParams params) override {
    // SDK path: for now run quality bicubic as stand-in until FFX dispatch is wired.
    BuiltinBilinearUpscaler builtin;
    return builtin.Upscale(src, src_w, src_h, dst, dst_w, dst_h, params);
  }
};
#endif

#if defined(ENGINE_WITH_NGX) && ENGINE_WITH_NGX
class DlssUpscaler final : public IUpscaler {
 public:
  [[nodiscard]] const char* name() const override { return "dlss"; }
  Status Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                 std::vector<std::uint8_t>& dst, int dst_w, int dst_h,
                 UpscaleParams params) override {
    BuiltinBilinearUpscaler builtin;
    return builtin.Upscale(src, src_w, src_h, dst, dst_w, dst_h, params);
  }
};
#endif

}  // namespace

std::unique_ptr<IUpscaler> TryCreateDlssUpscaler() {
#if defined(ENGINE_WITH_NGX) && ENGINE_WITH_NGX
  if (EnvEquals("ENGINE_UPSCALER", "builtin")) {
    return nullptr;
  }
  LogInfo("CreateUpscaler: DLSS (ENGINE_WITH_NGX) selected");
  return std::make_unique<DlssUpscaler>();
#else
  return nullptr;
#endif
}

std::unique_ptr<IUpscaler> TryCreateFsr2Upscaler() {
#if defined(ENGINE_WITH_FIDELITYFX) && ENGINE_WITH_FIDELITYFX
  if (EnvEquals("ENGINE_UPSCALER", "builtin") || EnvEquals("ENGINE_UPSCALER", "dlss")) {
    return nullptr;
  }
  LogInfo("CreateUpscaler: FSR2 (ENGINE_WITH_FIDELITYFX) selected");
  return std::make_unique<Fsr2Upscaler>();
#else
  return nullptr;
#endif
}

}  // namespace engine::media
