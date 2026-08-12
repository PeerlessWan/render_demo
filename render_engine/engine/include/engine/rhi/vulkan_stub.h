#pragma once

// M17/M18 placeholder — Vulkan device creation returns Unavailable until implemented.
namespace engine::rhi {
struct VulkanBackendInfo {
  static constexpr const char* kStatus = "skeleton";
  static constexpr bool kImplemented = false;
};
}  // namespace engine::rhi
