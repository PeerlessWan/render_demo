#pragma once

#include "engine/animation/skeleton.h"
#include "engine/core/result.h"

#include <filesystem>
#include <vector>

namespace engine::animation {

// Mega-W8 / C12: ephemeral Vulkan device + skin_cs_vk.spv compute.
// Feature-gated ("gpu_skinning"). When ENGINE_WITH_VULKAN=0 → Unavailable (SKIP).
// cs_spirv empty → resolve ENGINE_SHADER_DIR_A / skin_cs_vk.cs.spv when defined.
[[nodiscard]] bool TryDispatchGpuSkinVk(const std::vector<Vec3>& bind_positions,
                                        const SkinPose& pose, const std::vector<int>& bones4,
                                        const std::vector<float>& weights4,
                                        std::vector<Vec3>& out_positions,
                                        const std::filesystem::path& cs_spirv = {});

Status DispatchGpuSkinVkStatus(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                               const std::vector<int>& bones4, const std::vector<float>& weights4,
                               std::vector<Vec3>& out_positions,
                               const std::filesystem::path& cs_spirv = {});

}  // namespace engine::animation
