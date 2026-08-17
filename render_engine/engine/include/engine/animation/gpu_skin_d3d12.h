#pragma once

#include "engine/animation/skeleton.h"
#include "engine/core/result.h"

#include <filesystem>
#include <vector>

namespace engine::animation {

// W7/C12: ephemeral D3D12 device + skin_cs.cso PSO; skins a small buffer and reads back.
// Returns false / Unavailable when Feature off, no D3D12, or CS missing (VK SKIP).
// cs_dxil empty → resolve ENGINE_SHADER_DIR_A / skin_cs.cso when defined.
[[nodiscard]] bool TryDispatchGpuSkinD3d12(const std::vector<Vec3>& bind_positions,
                                           const SkinPose& pose, const std::vector<int>& bones4,
                                           const std::vector<float>& weights4,
                                           std::vector<Vec3>& out_positions,
                                           const std::filesystem::path& cs_dxil = {});

// Status variant for callers that need Unavailable vs Failed distinction.
Status DispatchGpuSkinD3d12Status(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                                  const std::vector<int>& bones4,
                                  const std::vector<float>& weights4,
                                  std::vector<Vec3>& out_positions,
                                  const std::filesystem::path& cs_dxil = {});

}  // namespace engine::animation
