#pragma once

#include "engine/media/upscaler.h"

#include <memory>

namespace engine::media {

// W22 / ADR 0045: DLSS → FSR2 → builtin. XeSS 不做.
// Factories return non-null only when Upscale() can succeed (vendor evaluate wired).
// BindUpscalerGpuDevice marks the live RHI device; without Bind+SDK, CreateUpscaler → bilinear.

enum class UpscalerGpuApi : int { None = 0, D3D12 = 1, Vulkan = 2 };

// Notify that a live GPU device exists (opaque pointer kept for future NGX/FFX context).
void BindUpscalerGpuDevice(UpscalerGpuApi api, void* native_device_or_null);
[[nodiscard]] bool UpscalerGpuDeviceBound();
[[nodiscard]] UpscalerGpuApi UpscalerBoundApi();

[[nodiscard]] std::unique_ptr<IUpscaler> TryCreateDlssUpscaler();
[[nodiscard]] std::unique_ptr<IUpscaler> TryCreateFsr2Upscaler();

}  // namespace engine::media
