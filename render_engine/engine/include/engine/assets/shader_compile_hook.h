#pragma once

#include "engine/core/result.h"

#include <filesystem>

namespace engine::assets {

// Mega-W10 / C16: optional DXC hook for HLSL → DXIL when `dxc` / `dxc.exe` is on PATH.
// Does not vendor DXC. Missing tool → Unavailable SKIP (honest).
// Sandbox / ShaderHotReload may call this optionally; offline tools/shader_compile remains primary.

[[nodiscard]] bool IsDxcOnPath();

// Try `dxc -T ps_6_0 -E main <path>` (or vs_6_0 if filename suggests VS).
// Ok when dxc runs successfully; Unavailable SKIP when dxc missing; Failed on compile error.
[[nodiscard]] Status TryCompileHlslWithDxc(const std::filesystem::path& hlsl_path);

}  // namespace engine::assets
