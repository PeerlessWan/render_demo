#include "engine/animation/gpu_skin_main.h"

#include "engine/animation/gpu_skin_d3d12.h"
#include "engine/animation/gpu_skin_vk.h"
#include "engine/core/feature.h"
#include "engine/core/log.h"

namespace engine::animation {

Status SkinOnDevice(rhi::IDevice& device, const std::vector<Vec3>& bind_positions,
                    const SkinPose& pose, const std::vector<int>& bones4,
                    const std::vector<float>& weights4, std::vector<Vec3>& out_positions) {
  if (!QueryFeature("gpu_skinning")) {
    SkinVerticesGpuDispatchStub(bind_positions, pose, bones4, weights4, out_positions);
    return Status::Ok("cpu-fallback: Feature gpu_skinning=false");
  }

  // Mega-W11: prefer the live device's API so Vulkan hosts do not silently take D3D12.
  const rhi::DeviceApiKind api = device.api_kind();
  if (api == rhi::DeviceApiKind::Vulkan) {
    const Status vk =
        DispatchGpuSkinVkStatus(bind_positions, pose, bones4, weights4, out_positions, {});
    if (vk) {
      return Status::Ok("gpu-skin-vk");
    }
    SkinVerticesGpuDispatchStub(bind_positions, pose, bones4, weights4, out_positions);
    LogInfo(std::string("SkinOnDevice: VK CS unavailable (") + vk.message() +
            "); CPU fallback (Vulkan device)");
    return Status::Ok("cpu-fallback: GPU CS unavailable");
  }

  if (api == rhi::DeviceApiKind::D3D12) {
    const Status d3d =
        DispatchGpuSkinD3d12Status(bind_positions, pose, bones4, weights4, out_positions, {});
    if (d3d) {
      return Status::Ok("gpu-skin-d3d12");
    }
    SkinVerticesGpuDispatchStub(bind_positions, pose, bones4, weights4, out_positions);
    LogInfo(std::string("SkinOnDevice: D3D12 CS unavailable (") + d3d.message() +
            "); CPU fallback (D3D12 device)");
    return Status::Ok("cpu-fallback: GPU CS unavailable");
  }

  // Headless / unknown: try D3D12 then Vulkan ephemeral CS (W9 behavior).
  const Status d3d =
      DispatchGpuSkinD3d12Status(bind_positions, pose, bones4, weights4, out_positions, {});
  if (d3d) {
    return Status::Ok("gpu-skin-d3d12");
  }

  const Status vk =
      DispatchGpuSkinVkStatus(bind_positions, pose, bones4, weights4, out_positions, {});
  if (vk) {
    return Status::Ok("gpu-skin-vk");
  }

  SkinVerticesGpuDispatchStub(bind_positions, pose, bones4, weights4, out_positions);
  LogInfo(std::string("SkinOnDevice: GPU CS unavailable (d3d=") + d3d.message() +
          "; vk=" + vk.message() + "); CPU fallback");
  return Status::Ok("cpu-fallback: GPU CS unavailable");
}

}  // namespace engine::animation
