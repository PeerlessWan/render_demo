#include "vulkan_device_internal.h"

namespace engine::rhi {

Result<std::unique_ptr<IDevice>> CreateVulkanDevice(const DeviceDesc& desc) {
  auto device = std::make_unique<VulkanDevice>();
  if (auto st = device->Init(desc); !st) {
    return Result<std::unique_ptr<IDevice>>::Fail(st);
  }
  return Result<std::unique_ptr<IDevice>>::Ok(std::move(device));
}

std::vector<GpuAdapterInfo> EnumerateVulkanAdapters() {
  std::vector<GpuAdapterInfo> out;
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) {
    return out;
  }
  std::uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  if (count > 0) {
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(devices[i], &props);
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);
    GpuAdapterInfo info;
    info.index = static_cast<int>(i);
    info.name = props.deviceName;
    info.discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    info.software = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    for (std::uint32_t mi = 0; mi < mem.memoryHeapCount; ++mi) {
      if (mem.memoryHeaps[mi].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        info.dedicated_memory_bytes += mem.memoryHeaps[mi].size;
      }
    }
    out.push_back(std::move(info));
  }
  vkDestroyInstance(instance, nullptr);
  return out;
}

}  // namespace engine::rhi
