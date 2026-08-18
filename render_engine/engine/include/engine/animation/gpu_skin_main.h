#pragma once

#include "engine/animation/skeleton.h"
#include "engine/core/result.h"
#include "engine/rhi/i_device.h"

#include <vector>

namespace engine::animation {

// Mega-W9 / C12: main-path GPU skin entry on a live IDevice.
// Prefers existing D3D12/Vulkan skin CS (ephemeral dispatch) when Feature
// "gpu_skinning" is on; otherwise documents CPU fallback via Status message
// and fills out_positions with SkinVerticesGpuDispatchStub.
//
// device is reserved for future in-place CS on the frame device; current
// implementations reuse TryDispatchGpuSkinD3d12 / TryDispatchGpuSkinVk.
[[nodiscard]] Status SkinOnDevice(rhi::IDevice& device, const std::vector<Vec3>& bind_positions,
                                  const SkinPose& pose, const std::vector<int>& bones4,
                                  const std::vector<float>& weights4,
                                  std::vector<Vec3>& out_positions);

}  // namespace engine::animation
