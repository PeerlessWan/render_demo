#pragma once

#include "engine/core/result.h"

#include <filesystem>
#include <string_view>

namespace engine::assets {

// Mega-W10 / C16: optional DXC hook for HLSL → DXIL when `dxc` / `dxc.exe` is on PATH.
// Does not vendor DXC. Missing tool → Unavailable SKIP (honest).
// W18: can write `-Fo` to out_cso; entry/profile default from filename (VSMain/PSMain/CSMain).

[[nodiscard]] bool IsDxcOnPath();

// Probe compile (NUL output) with inferred entry/profile.
[[nodiscard]] Status TryCompileHlslWithDxc(const std::filesystem::path& hlsl_path);

// W18: compile with explicit entry/profile; if out_cso non-empty, write DXIL there.
[[nodiscard]] Status TryCompileHlslWithDxc(const std::filesystem::path& hlsl_path,
                                           const std::filesystem::path& out_cso,
                                           std::string_view entry = {},
                                           std::string_view profile = {});

}  // namespace engine::assets
