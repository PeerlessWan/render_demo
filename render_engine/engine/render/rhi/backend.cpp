#include "engine/rhi/backend.h"

namespace engine::rhi {

Result<std::unique_ptr<IDevice>> CreateDevice(Backend backend, const DeviceDesc& desc) {
  switch (backend) {
    case Backend::D3D12:
      if (desc.headless || desc.native_window == nullptr) {
        return CreateHeadlessDevice(desc);
      }
      return CreateD3D12Device(desc);
    case Backend::Vulkan:
      if (desc.headless) {
        return CreateHeadlessDevice(desc);
      }
      return Result<std::unique_ptr<IDevice>>::Fail(
          Status::Fail(ErrorCode::Unavailable,
                       "Vulkan backend not linked yet (M17 skeleton; use --backend=d3d12)"));
  }
  return Result<std::unique_ptr<IDevice>>::Fail("unknown backend");
}

}  // namespace engine::rhi
