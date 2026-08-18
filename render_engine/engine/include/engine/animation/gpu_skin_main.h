#pragma once

#include "engine/animation/skeleton.h"
#include "engine/core/result.h"
#include "engine/rhi/i_device.h"

#include <vector>

namespace engine::animation {

// Mega-W9/W11 / C12: main-path GPU skin entry on a live IDevice.
// When Feature "gpu_skinning" is on, routes by device.api_kind(): Vulkan →
// gpu_skin_vk, D3D12 → gpu_skin_d3d12, Headless → try both. Else CPU fallback
// via Status message + SkinVerticesGpuDispatchStub.
//
// Ephemeral CS devices today; api_kind ensures VK hosts do not prefer D3D12.
[[nodiscard]] Status SkinOnDevice(rhi::IDevice& device, const std::vector<Vec3>& bind_positions,
                                  const SkinPose& pose, const std::vector<int>& bones4,
                                  const std::vector<float>& weights4,
                                  std::vector<Vec3>& out_positions);

}  // namespace engine::animation
