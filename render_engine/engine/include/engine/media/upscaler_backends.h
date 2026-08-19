#pragma once

#include "engine/media/upscaler.h"

#include <memory>

namespace engine::media {

// W12 / ADR 0039: DLSS → FSR2 → builtin. Factories return nullptr when SDK absent
// (CreateUpscaler logs once and falls through). Never claim FSR/DLSS without SDK.

[[nodiscard]] std::unique_ptr<IUpscaler> TryCreateDlssUpscaler();
[[nodiscard]] std::unique_ptr<IUpscaler> TryCreateFsr2Upscaler();

}  // namespace engine::media
