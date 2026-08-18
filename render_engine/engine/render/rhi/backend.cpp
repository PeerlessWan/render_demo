#include "engine/rhi/backend.h"

namespace engine::rhi {

#if !defined(_WIN32)
Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc&) {
  return Result<std::unique_ptr<IDevice>>::Fail(
      Status::Fail(ErrorCode::Unavailable, "D3D12 unavailable on non-Windows (use --backend=vulkan)"));
}

std::vector<GpuAdapterInfo> EnumerateD3D12Adapters() { return {}; }
#endif

Result<std::unique_ptr<IDevice>> CreateDevice(Backend backend, const DeviceDesc& desc) {
  switch (backend) {
    case Backend::D3D12:
#if defined(_WIN32)
      // Prefer real GPU offscreen when requested (CI regression / automated readback).
      if (desc.gpu_headless) {
        return CreateD3D12Device(desc);
      }
      if (desc.headless || desc.native_window == nullptr) {
        return CreateHeadlessDevice(desc);
      }
      return CreateD3D12Device(desc);
#else
      (void)desc;
      return Result<std::unique_ptr<IDevice>>::Fail(
          Status::Fail(ErrorCode::Unavailable,
                       "D3D12 backend not available on this platform (use --backend=vulkan)"));
#endif
    case Backend::Vulkan:
      // CPU stub: unit tests / --headless without GPU. gpu_headless wants real Vk + HWND/X11.
      if (desc.headless && !desc.gpu_headless) {
        return CreateHeadlessDevice(desc);
      }
      if (desc.gpu_headless && desc.native_window == nullptr) {
        return CreateHeadlessDevice(desc);
      }
#if defined(ENGINE_WITH_VULKAN) && ENGINE_WITH_VULKAN
      return CreateVulkanDevice(desc);
#else
      return Result<std::unique_ptr<IDevice>>::Fail(
          Status::Fail(ErrorCode::Unavailable,
                       "Vulkan backend not enabled (ENGINE_WITH_VULKAN=0; use --backend=d3d12)"));
#endif
  }
  return Result<std::unique_ptr<IDevice>>::Fail("unknown backend");
}

std::vector<GpuAdapterInfo> EnumerateGpuAdapters(Backend backend) {
  switch (backend) {
    case Backend::D3D12:
      return EnumerateD3D12Adapters();
    case Backend::Vulkan:
#if defined(ENGINE_WITH_VULKAN) && ENGINE_WITH_VULKAN
      return EnumerateVulkanAdapters();
#else
      return {};
#endif
  }
  return {};
}

}  // namespace engine::rhi
