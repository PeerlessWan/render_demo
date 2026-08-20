#include "gpu_compute_oneshot_vk.h"

namespace engine::rhi::gpu_compute {

VulkanComputeOneShot::VulkanComputeOneShot(VkDevice device, VkQueue queue, VkCommandPool pool)
    : device_(device), queue_(queue), pool_(pool) {}

bool VulkanComputeOneShot::Run(const std::function<void(VkCommandBuffer)>& record) {
  if (!valid() || !record) {
    return false;
  }
  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS || cmd == VK_NULL_HANDLE) {
    return false;
  }
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
    vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    return false;
  }
  record(cmd);
  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    return false;
  }
  VkFenceCreateInfo fi{};
  fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  if (vkCreateFence(device_, &fi, nullptr, &fence) != VK_SUCCESS) {
    vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    return false;
  }
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  const bool ok = vkQueueSubmit(queue_, 1, &si, fence) == VK_SUCCESS &&
                  vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
  vkDestroyFence(device_, fence, nullptr);
  vkFreeCommandBuffers(device_, pool_, 1, &cmd);
  return ok;
}

}  // namespace engine::rhi::gpu_compute
