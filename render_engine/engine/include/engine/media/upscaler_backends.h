#pragma once

#include "engine/media/upscaler.h"

#include <memory>

namespace engine::media {

// W12 / ADR 0039 / ADR 0044: DLSS → FSR2 → builtin. Factories return nullptr when SDK
// absent or GPU dispatch not bound (CreateUpscaler logs once and falls through).
// Never claim FSR/DLSS without a real Upscale() vendor path.

[[nodiscard]] std::unique_ptr<IUpscaler> TryCreateDlssUpscaler();
[[nodiscard]] std::unique_ptr<IUpscaler> TryCreateFsr2Upscaler();

}  // namespace engine::media
