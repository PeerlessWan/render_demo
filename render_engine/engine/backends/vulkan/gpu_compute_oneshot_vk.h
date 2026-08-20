#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

namespace engine::rhi::gpu_compute {

// W20: one-shot primary command buffer + submit + fence (mirrors D3D12ComputeOneShot).
class VulkanComputeOneShot {
 public:
  VulkanComputeOneShot(VkDevice device, VkQueue queue, VkCommandPool pool);

  [[nodiscard]] bool valid() const {
    return device_ != VK_NULL_HANDLE && queue_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE;
  }

  bool Run(const std::function<void(VkCommandBuffer)>& record);

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
};

}  // namespace engine::rhi::gpu_compute
