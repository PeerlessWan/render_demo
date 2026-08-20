#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine::media {

// Optional sample-UV offset (NDC-style, typically small). Sandbox/TAA write these via
// EffectTuning; BuiltinBilinearUpscaler applies them as a subpixel UV shift (TAA-like path).
struct UpscaleParams {
  float jitter_x = 0.f;
  float jitter_y = 0.f;
};

// Map display size → internal render size for resolution-scale path (ADR 0008).
// Part of the FSR-absent fallback together with BuiltinBilinearUpscaler.
struct ResolutionScale {
  // Clamp to a practical internal range (0.5 .. 1.0). Values >= 1 keep native size.
  static float ClampScale(float scale);
  static void ComputeRenderSize(int display_w, int display_h, float scale, int& out_w, int& out_h);
};

class IUpscaler {
 public:
  virtual ~IUpscaler() = default;
  [[nodiscard]] virtual const char* name() const = 0;
  // Upscale rgba to out buffer. Returns Fail on invalid dims; Ok on pass-through or scale.
  virtual Status Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                         std::vector<std::uint8_t>& dst, int dst_w, int dst_h,
                         UpscaleParams params = {}) = 0;
};

// CPU bilinear placeholder — FSR-absent fallback (ADR 0008). Not FSR; name stays
// "builtin_bilinear". Jitter offsets sample UVs for TAA-like documentation path.
class BuiltinBilinearUpscaler final : public IUpscaler {
 public:
  [[nodiscard]] const char* name() const override { return "builtin_bilinear"; }
  Status Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                 std::vector<std::uint8_t>& dst, int dst_w, int dst_h,
                 UpscaleParams params = {}) override;
};

// Preference: ENGINE_UPSCALER=fsr|fsr2|dlss; chain DLSS→FSR2→builtin (ADR 0044).
// Without SDK, logs once and returns builtin_bilinear (never fake vendor names).
std::unique_ptr<IUpscaler> CreateUpscaler();

}  // namespace engine::media
