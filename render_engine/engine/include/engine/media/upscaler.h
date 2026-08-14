#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine::media {

class IUpscaler {
 public:
  virtual ~IUpscaler() = default;
  [[nodiscard]] virtual const char* name() const = 0;
  // Upscale rgba in-place or to out buffer. Returns false if dimensions unchanged.
  virtual Status Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                         std::vector<std::uint8_t>& dst, int dst_w, int dst_h) = 0;
};

// CPU bilinear placeholder; real DLSS/FSR adapters plug in later (ADR 0008).
class BuiltinBilinearUpscaler final : public IUpscaler {
 public:
  [[nodiscard]] const char* name() const override { return "builtin_bilinear"; }
  Status Upscale(std::span<const std::uint8_t> src, int src_w, int src_h,
                 std::vector<std::uint8_t>& dst, int dst_w, int dst_h) override;
};

std::unique_ptr<IUpscaler> CreateUpscaler();

}  // namespace engine::media
