#include "engine/rhi/i_device.h"

#include "engine/core/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace engine::rhi {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;

std::string VkErr(VkResult r) {
  return "VkResult=" + std::to_string(static_cast<int>(r));
}

class VulkanDevice final : public IDevice {
 public:
  Status Init(const DeviceDesc& desc) {
    if (!desc.native_window || desc.width == 0 || desc.height == 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid DeviceDesc for Vulkan");
    }
    hwnd_ = static_cast<HWND>(desc.native_window);
    width_ = desc.width;
    height_ = desc.height;

    if (auto st = CreateInstance(); !st) {
      return st;
    }
    if (auto st = CreateSurface(); !st) {
      return st;
    }
    if (auto st = PickPhysicalDevice(); !st) {
      return st;
    }
    if (auto st = CreateLogicalDevice(); !st) {
      return st;
    }
    if (auto st = CreateSwapchain(); !st) {
      return st;
    }
    if (auto st = CreateFrameSync(); !st) {
      return st;
    }

    LogInfo("Vulkan device ready (Win32 surface + swapchain clear)");
    return Status::Ok();
  }

  ~VulkanDevice() override {
    if (device_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
    }
    DestroySwapchain();
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      if (in_flight_fences_[i] != VK_NULL_HANDLE) {
        vkDestroyFence(device_, in_flight_fences_[i], nullptr);
      }
      if (render_finished_[i] != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, render_finished_[i], nullptr);
      }
      if (image_available_[i] != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, image_available_[i], nullptr);
      }
    }
    if (command_pool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, command_pool_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE) {
      vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
      vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
      vkDestroyInstance(instance_, nullptr);
    }
  }

  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }

  Status BeginFrame() override {
    VkFence fence = in_flight_fences_[frame_index_];
    VkResult r = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkWaitForFences failed: " + VkErr(r));
    }

    r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_[frame_index_],
                              VK_NULL_HANDLE, &image_index_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
      return RecreateSwapchain();
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
      return Status::Fail("vkAcquireNextImageKHR failed: " + VkErr(r));
    }

    r = vkResetFences(device_, 1, &fence);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkResetFences failed: " + VkErr(r));
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    r = vkResetCommandBuffer(cmd, 0);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkResetCommandBuffer failed: " + VkErr(r));
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &begin);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkBeginCommandBuffer failed: " + VkErr(r));
    }

    frame_recording_ = true;
    cleared_ = false;
    return Status::Ok();
  }

  Status Clear(const ColorRgba& color) override {
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    VkCommandBuffer cmd = command_buffers_[frame_index_];
    VkImage image = swapchain_images_[image_index_];

    VkImageMemoryBarrier to_clear{};
    to_clear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_clear.srcAccessMask = 0;
    to_clear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_clear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_clear.image = image;
    to_clear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_clear.subresourceRange.levelCount = 1;
    to_clear.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &to_clear);

    VkClearColorValue clear{};
    clear.float32[0] = color.r;
    clear.float32[1] = color.g;
    clear.float32[2] = color.b;
    clear.float32[3] = color.a;
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

    VkImageMemoryBarrier to_present{};
    to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image = image;
    to_present.subresourceRange = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_present);

    cleared_ = true;
    return Status::Ok();
  }

  Status Present() override {
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (!cleared_) {
      // Ensure presentable layout even if Clear was skipped.
      if (auto st = Clear({0.f, 0.f, 0.f, 1.f}); !st) {
        return st;
      }
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    VkResult r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkEndCommandBuffer failed: " + VkErr(r));
    }
    frame_recording_ = false;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available_[frame_index_];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_[frame_index_];

    r = vkQueueSubmit(graphics_queue_, 1, &submit, in_flight_fences_[frame_index_]);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkQueueSubmit failed: " + VkErr(r));
    }

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished_[frame_index_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index_;
    r = vkQueuePresentKHR(present_queue_, &present);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
      if (auto st = RecreateSwapchain(); !st) {
        return st;
      }
    } else if (r != VK_SUCCESS) {
      return Status::Fail("vkQueuePresentKHR failed: " + VkErr(r));
    }

    frame_index_ = (frame_index_ + 1) % kFramesInFlight;
    return Status::Ok();
  }

  Status Resize(std::uint32_t width, std::uint32_t height) override {
    if (width == 0 || height == 0) {
      return Status::Ok();
    }
    if (width == width_ && height == height_) {
      return Status::Ok();
    }
    width_ = width;
    height_ = height;
    return RecreateSwapchain();
  }

  Status DrawSimpleMesh() override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status SetupSimpleMesh(const SimpleMeshShaders&) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status SetupLitMesh(const LitMeshShaders&) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status SetFrameLighting(const FrameLighting&) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status BeginShadowPass() override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status BindShadowCascade(int) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status DrawShadowCubes(std::span<const LitDrawItem>) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status EndShadowPass() override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status BeginLocalShadowPass() override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status BindLocalShadowTile(int) override {
    return Status::Ok();
  }
  Status EndLocalShadowPass() override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status SetupPostMesh(const PostShaders&) override {
    return Status::Ok();
  }
  Status ResolvePostEffects(const PostResolveDesc&) override {
    return Status::Ok();
  }
  Status DrawLitCube(const LitDrawItem&) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status DrawLitCubes(std::span<const LitDrawItem>) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status UploadLitAlbedoRgba(const std::uint8_t*, int, int) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status UploadLitOrmRgba(const std::uint8_t*, int, int) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status DrawScreenQuads(std::span<const ScreenQuad>) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status SetupUiMesh(const SimpleMeshShaders&) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status UploadUiFontAtlas(const std::uint8_t*, int, int) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status DrawUiMesh(std::span<const UiVertex>, std::span<const std::uint16_t>,
                    std::span<const UiDrawCmd>) override {
    return Status::Fail("not implemented on Vulkan path yet");
  }
  Status DispatchCompute(const ComputeDispatchDesc& desc) override {
    if (desc.groups_x == 0 || desc.groups_y == 0 || desc.groups_z == 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "compute groups must be > 0");
    }
    return Status::Ok();  // accepted; no GPU compute yet
  }
  Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override {
    w = static_cast<int>(width_);
    h = static_cast<int>(height_);
    out_rgba.assign(static_cast<std::size_t>(w * h * 4), 0);
    return Status::Ok();
  }

 private:
  Status CreateInstance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "render_engine";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "render_engine";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = exts;

    const VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkCreateInstance failed: " + VkErr(r));
    }
    return Status::Ok();
  }

  Status CreateSurface() {
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = hwnd_;
    const VkResult r = vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkCreateWin32SurfaceKHR failed: " + VkErr(r));
    }
    return Status::Ok();
  }

  Status PickPhysicalDevice() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
      return Status::Fail("No Vulkan physical devices");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (VkPhysicalDevice pd : devices) {
      std::uint32_t qcount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
      std::vector<VkQueueFamilyProperties> qprops(qcount);
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());

      std::int32_t graphics = -1;
      std::int32_t present = -1;
      for (std::uint32_t i = 0; i < qcount; ++i) {
        if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
          graphics = static_cast<std::int32_t>(i);
        }
        VkBool32 support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &support);
        if (support) {
          present = static_cast<std::int32_t>(i);
        }
        if (graphics >= 0 && present >= 0) {
          break;
        }
      }
      if (graphics < 0 || present < 0) {
        continue;
      }

      std::uint32_t ext_count = 0;
      vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
      std::vector<VkExtensionProperties> exts(ext_count);
      vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
      bool has_swapchain = false;
      for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
          has_swapchain = true;
          break;
        }
      }
      if (!has_swapchain) {
        continue;
      }

      physical_ = pd;
      graphics_family_ = static_cast<std::uint32_t>(graphics);
      present_family_ = static_cast<std::uint32_t>(present);
      break;
    }

    if (physical_ == VK_NULL_HANDLE) {
      return Status::Fail("No suitable Vulkan GPU with swapchain + present");
    }
    return Status::Ok();
  }

  Status CreateLogicalDevice() {
    const float priority = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    const std::uint32_t families[] = {graphics_family_, present_family_};
    for (std::uint32_t fam : families) {
      bool exists = false;
      for (const auto& q : qcis) {
        if (q.queueFamilyIndex == fam) {
          exists = true;
          break;
        }
      }
      if (exists) {
        continue;
      }
      VkDeviceQueueCreateInfo qci{};
      qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      qci.queueFamilyIndex = fam;
      qci.queueCount = 1;
      qci.pQueuePriorities = &priority;
      qcis.push_back(qci);
    }

    const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<std::uint32_t>(qcis.size());
    ci.pQueueCreateInfos = qcis.data();
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = exts;

    const VkResult r = vkCreateDevice(physical_, &ci, nullptr, &device_);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkCreateDevice failed: " + VkErr(r));
    }
    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);

    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = graphics_family_;
    if (vkCreateCommandPool(device_, &pool, nullptr, &command_pool_) != VK_SUCCESS) {
      return Status::Fail("vkCreateCommandPool failed");
    }

    command_buffers_.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(device_, &alloc, command_buffers_.data()) != VK_SUCCESS) {
      return Status::Fail("vkAllocateCommandBuffers failed");
    }
    return Status::Ok();
  }

  Status CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);

    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = f;
        break;
      }
    }
    surface_format_ = chosen;

    std::uint32_t present_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_count, nullptr);
    std::vector<VkPresentModeKHR> modes(present_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_count, modes.data());
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes) {
      if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
        present_mode = m;
        break;
      }
    }

    VkExtent2D extent{};
    if (caps.currentExtent.width != UINT32_MAX) {
      extent = caps.currentExtent;
    } else {
      extent.width = std::clamp(width_, caps.minImageExtent.width, caps.maxImageExtent.width);
      extent.height = std::clamp(height_, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    width_ = extent.width;
    height_ = extent.height;

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
      image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = image_count;
    ci.imageFormat = surface_format_.format;
    ci.imageColorSpace = surface_format_.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const std::uint32_t qf[] = {graphics_family_, present_family_};
    if (graphics_family_ != present_family_) {
      ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      ci.queueFamilyIndexCount = 2;
      ci.pQueueFamilyIndices = qf;
    } else {
      ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present_mode;
    ci.clipped = VK_TRUE;

    const VkResult r = vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkCreateSwapchainKHR failed: " + VkErr(r));
    }

    std::uint32_t img_count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, nullptr);
    swapchain_images_.resize(img_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, swapchain_images_.data());

    swapchain_views_.resize(img_count);
    for (std::uint32_t i = 0; i < img_count; ++i) {
      VkImageViewCreateInfo vi{};
      vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      vi.image = swapchain_images_[i];
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = surface_format_.format;
      vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      vi.subresourceRange.levelCount = 1;
      vi.subresourceRange.layerCount = 1;
      if (vkCreateImageView(device_, &vi, nullptr, &swapchain_views_[i]) != VK_SUCCESS) {
        return Status::Fail("vkCreateImageView failed");
      }
    }
    return Status::Ok();
  }

  Status CreateFrameSync() {
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      if (vkCreateSemaphore(device_, &sci, nullptr, &image_available_[i]) != VK_SUCCESS ||
          vkCreateSemaphore(device_, &sci, nullptr, &render_finished_[i]) != VK_SUCCESS ||
          vkCreateFence(device_, &fci, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
        return Status::Fail("Create frame sync objects failed");
      }
    }
    return Status::Ok();
  }

  void DestroySwapchain() {
    for (VkImageView v : swapchain_views_) {
      if (v != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, v, nullptr);
      }
    }
    swapchain_views_.clear();
    swapchain_images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
      swapchain_ = VK_NULL_HANDLE;
    }
  }

  Status RecreateSwapchain() {
    if (device_ == VK_NULL_HANDLE) {
      return Status::Fail("No device");
    }
    vkDeviceWaitIdle(device_);
    DestroySwapchain();
    return CreateSwapchain();
  }

  HWND hwnd_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkQueue present_queue_ = VK_NULL_HANDLE;
  std::uint32_t graphics_family_ = 0;
  std::uint32_t present_family_ = 0;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkSurfaceFormatKHR surface_format_{};
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_views_;

  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> command_buffers_;

  std::array<VkSemaphore, kFramesInFlight> image_available_{};
  std::array<VkSemaphore, kFramesInFlight> render_finished_{};
  std::array<VkFence, kFramesInFlight> in_flight_fences_{};

  std::uint32_t frame_index_ = 0;
  std::uint32_t image_index_ = 0;
  bool frame_recording_ = false;
  bool cleared_ = false;
};

}  // namespace

Result<std::unique_ptr<IDevice>> CreateVulkanDevice(const DeviceDesc& desc) {
  auto device = std::make_unique<VulkanDevice>();
  if (auto st = device->Init(desc); !st) {
    return Result<std::unique_ptr<IDevice>>::Fail(st);
  }
  return Result<std::unique_ptr<IDevice>>::Ok(std::move(device));
}

}  // namespace engine::rhi
