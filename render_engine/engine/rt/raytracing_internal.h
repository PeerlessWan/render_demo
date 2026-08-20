#pragma once

#include "engine/rt/raytracing.h"

#include <filesystem>
#include <vector>

namespace engine::rt {
namespace rt_detail {

std::filesystem::path ResolveDxrLibPath(const std::filesystem::path& override_path);

#if defined(_WIN32)
Status TryBuildCubeBlasTlasAndDispatchRaysWin(const std::filesystem::path& dxr_lib_dxil);
#endif

}  // namespace rt_detail
}  // namespace engine::rt
