#pragma once

#include "engine/core/result.h"
#include "engine/rhi/i_device.h"

#include <memory>
#include <string_view>

namespace engine::rhi {

enum class Backend { D3D12, Vulkan };

Result<std::unique_ptr<IDevice>> CreateDevice(Backend backend, const DeviceDesc& desc);

}  // namespace engine::rhi
