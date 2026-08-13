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
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace engine::rhi {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr std::uint32_t kMaxLitDraws = 64;
constexpr VkDeviceSize kUniformAlign = 256;
constexpr std::uint32_t kShadowMapSize = 2048;

std::string VkErr(VkResult r) {
  return "VkResult=" + std::to_string(static_cast<int>(r));
}

Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    return Result<std::vector<std::uint8_t>>::Fail("Cannot open shader: " + path.string());
  }
  const auto size = static_cast<std::size_t>(f.tellg());
  if (size == 0) {
    return Result<std::vector<std::uint8_t>>::Fail("Empty shader: " + path.string());
  }
  std::vector<std::uint8_t> data(size);
  f.seekg(0);
  f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
  return Result<std::vector<std::uint8_t>>::Ok(std::move(data));
}

struct FrameGpu {
  float view_proj[16];
  float cascade_vp[4][16];
  float sun_dir[3];
  float sun_intensity;
  float ambient[3];
  float shadow_bias;
  float sun_color[3];
  float specular_power;
  float eye[3];
  float enable_shadow;
  float cascade_splits[4];
  float cam_forward[3];
  float cascade_count;
  float tiles_per_row;
  float pad[3];
};

struct ShadowFrameGpu {
  float view_proj[16];
};

struct ObjectGpu {
  float world[16];
  float color[4];
  float metallic;
  float roughness;
  float pad[2];
};

constexpr VkDeviceSize kFrameUbSize =
    (sizeof(FrameGpu) + kUniformAlign - 1) / kUniformAlign * kUniformAlign;

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
    DestroyLitResources();
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
    pass_active_ = false;
    shadow_pass_active_ = false;
    lit_draws_this_frame_ = 0;
    return Status::Ok();
  }

  Status Clear(const ColorRgba& color) override {
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    clear_color_ = color;

    if (lit_ready_) {
      // Defer color pass so shadow pass can run first; DrawLitCubes begins RP.
      cleared_ = true;
      return Status::Ok();
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
    used_graphics_ = false;
    return Status::Ok();
  }

  Status Present() override {
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (!cleared_) {
      if (auto st = Clear({0.f, 0.f, 0.f, 1.f}); !st) {
        return st;
      }
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    if (shadow_pass_active_) {
      vkCmdEndRenderPass(cmd);
      shadow_pass_active_ = false;
      if (shadow_image_ != VK_NULL_HANDLE) {
        BarrierShadowImage(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
      }
    }
    if (lit_ready_ && !pass_active_) {
      if (auto st = BeginLitRenderPass(clear_color_); !st) {
        return st;
      }
    }
    if (pass_active_) {
      vkCmdEndRenderPass(cmd);
      pass_active_ = false;
    }

    VkResult r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkEndCommandBuffer failed: " + VkErr(r));
    }
    frame_recording_ = false;

    VkPipelineStageFlags wait_stage =
        used_graphics_ ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                       : VK_PIPELINE_STAGE_TRANSFER_BIT;
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
    return Status::Fail("Vulkan lit not ready");
  }
  Status SetupSimpleMesh(const SimpleMeshShaders&) override {
    return Status::Fail("Vulkan lit not ready");
  }

  Status SetupLitMesh(const LitMeshShaders& shaders) override {
    vkDeviceWaitIdle(device_);
    DestroyLitResources();

    auto vs = ReadFileBytes(shaders.vs_dxil);
    if (!vs) {
      return vs.status();
    }
    auto ps = ReadFileBytes(shaders.ps_dxil);
    if (!ps) {
      return ps.status();
    }
    auto shadow_vs = ReadFileBytes(shaders.shadow_vs_dxil);
    if (!shadow_vs) {
      return shadow_vs.status();
    }

    if (auto st = CreateRenderPass(); !st) {
      return st;
    }
    if (auto st = CreateDepthResources(); !st) {
      return st;
    }
    if (auto st = CreateFramebuffers(); !st) {
      return st;
    }
    if (auto st = CreateLitPipeline(vs.value(), ps.value()); !st) {
      return st;
    }
    if (auto st = CreateShadowResources(shadow_vs.value()); !st) {
      return st;
    }
    if (auto st = CreateCubeMesh(); !st) {
      return st;
    }
    if (auto st = CreateLitBuffersAndDescriptors(); !st) {
      return st;
    }

    lit_ready_ = true;
    LogInfo("Vulkan lit cube ready (depth + CSM shadows)");
    return Status::Ok();
  }

  Status SetFrameLighting(const FrameLighting& lighting) override {
    if (!lit_ready_ || !frame_ub_) {
      return Status::Fail("SetupLitMesh not called");
    }
    // Engine Mat4::Perspective is OpenGL NDC Z [-1,1]; Vulkan depth is [0,1].
    Mat4 clip_fix = Mat4::Identity();
    clip_fix.m[10] = 0.5f;
    clip_fix.m[14] = 0.5f;

    lighting_ = lighting;
    lighting_.view_proj = clip_fix * lighting.view_proj;
    lighting_.light_view_proj = clip_fix * lighting.light_view_proj;
    for (int i = 0; i < 4; ++i) {
      lighting_.cascade_view_proj[static_cast<std::size_t>(i)] =
          clip_fix * lighting.cascade_view_proj[static_cast<std::size_t>(i)];
    }

    FrameGpu data{};
    std::memcpy(data.view_proj, lighting_.view_proj.m.data(), sizeof(data.view_proj));
    for (int i = 0; i < 4; ++i) {
      std::memcpy(data.cascade_vp[i],
                  lighting_.cascade_view_proj[static_cast<std::size_t>(i)].m.data(),
                  sizeof(data.cascade_vp[i]));
    }
    data.sun_dir[0] = lighting_.sun_direction.x;
    data.sun_dir[1] = lighting_.sun_direction.y;
    data.sun_dir[2] = lighting_.sun_direction.z;
    data.sun_intensity = lighting_.sun_intensity;
    data.ambient[0] = lighting_.ambient.r;
    data.ambient[1] = lighting_.ambient.g;
    data.ambient[2] = lighting_.ambient.b;
    data.shadow_bias = lighting_.shadow_bias;
    data.sun_color[0] = lighting_.sun_color.r;
    data.sun_color[1] = lighting_.sun_color.g;
    data.sun_color[2] = lighting_.sun_color.b;
    data.specular_power = lighting_.specular_power;
    data.eye[0] = lighting_.eye.x;
    data.eye[1] = lighting_.eye.y;
    data.eye[2] = lighting_.eye.z;
    data.enable_shadow = lighting_.enable_shadows ? 1.f : 0.f;
    for (int i = 0; i < 4; ++i) {
      data.cascade_splits[i] = lighting_.cascade_splits[static_cast<std::size_t>(i)];
    }
    data.cam_forward[0] = lighting_.camera_forward.x;
    data.cam_forward[1] = lighting_.camera_forward.y;
    data.cam_forward[2] = lighting_.camera_forward.z;
    data.cascade_count = static_cast<float>(lighting_.cascade_count);
    data.tiles_per_row = static_cast<float>(lighting_.cascade_tiles_per_row);

    void* mapped = nullptr;
    if (vkMapMemory(device_, frame_ub_mem_, 0, sizeof(data), 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map frame UB failed");
    }
    std::memcpy(mapped, &data, sizeof(data));
    vkUnmapMemory(device_, frame_ub_mem_);
    bound_cascade_ = -1;
    return Status::Ok();
  }

  Status BeginShadowPass() override {
    if (!lit_ready_ || shadow_image_ == VK_NULL_HANDLE) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    VkCommandBuffer cmd = command_buffers_[frame_index_];
    if (pass_active_) {
      vkCmdEndRenderPass(cmd);
      pass_active_ = false;
    }

    if (shadow_layout_ != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      BarrierShadowImage(cmd, shadow_layout_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    VkClearValue clear{};
    clear.depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = shadow_render_pass_;
    rp.framebuffer = shadow_framebuffer_;
    rp.renderArea.extent = {kShadowMapSize, kShadowMapSize};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    shadow_pass_active_ = true;
    bound_cascade_ = -1;
    used_graphics_ = true;
    return Status::Ok();
  }

  Status BindShadowCascade(int cascade_index) override {
    if (!lit_ready_ || !shadow_pass_active_) {
      return Status::Fail("BeginShadowPass not active");
    }
    if (cascade_index < 0 || cascade_index >= lighting_.cascade_count) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid cascade index");
    }

    ShadowFrameGpu frame{};
    std::memcpy(frame.view_proj,
                lighting_.cascade_view_proj[static_cast<std::size_t>(cascade_index)].m.data(),
                sizeof(frame.view_proj));
    void* mapped = nullptr;
    if (vkMapMemory(device_, shadow_frame_ub_mem_, 0, sizeof(frame), 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map shadow frame UB failed");
    }
    std::memcpy(mapped, &frame, sizeof(frame));
    vkUnmapMemory(device_, shadow_frame_ub_mem_);

    const int tiles_per_row = (std::max)(1, lighting_.cascade_tiles_per_row);
    const float tile = static_cast<float>(kShadowMapSize) / static_cast<float>(tiles_per_row);
    const int ix = cascade_index % tiles_per_row;
    const int iy = cascade_index / tiles_per_row;

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    VkViewport vp{};
    vp.x = static_cast<float>(ix) * tile;
    vp.y = static_cast<float>(iy) * tile;
    vp.width = tile;
    vp.height = tile;
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset.x = static_cast<std::int32_t>(vp.x);
    scissor.offset.y = static_cast<std::int32_t>(vp.y);
    scissor.extent.width = static_cast<std::uint32_t>(tile);
    scissor.extent.height = static_cast<std::uint32_t>(tile);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    bound_cascade_ = cascade_index;
    return Status::Ok();
  }

  Status DrawShadowCubes(std::span<const LitDrawItem> items) override {
    if (!lit_ready_ || !shadow_pass_active_) {
      return Status::Fail("BeginShadowPass not active");
    }
    if (items.empty()) {
      return Status::Ok();
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);

    const VkDeviceSize vb_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &cube_vb_, &vb_offset);
    vkCmdBindIndexBuffer(cmd, cube_ib_, 0, VK_INDEX_TYPE_UINT16);

    for (std::size_t i = 0; i < items.size(); ++i) {
      ObjectGpu od{};
      std::memcpy(od.world, items[i].world.m.data(), sizeof(od.world));

      const VkDeviceSize slot = static_cast<VkDeviceSize>(i % kMaxLitDraws) * kUniformAlign;
      void* mapped = nullptr;
      if (vkMapMemory(device_, object_ub_mem_, slot, sizeof(od), 0, &mapped) != VK_SUCCESS) {
        return Status::Fail("Map object UB failed");
      }
      std::memcpy(mapped, &od, sizeof(od));
      vkUnmapMemory(device_, object_ub_mem_);

      const std::uint32_t dyn_offset = static_cast<std::uint32_t>(slot);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_layout_, 0, 1,
                              &shadow_desc_set_, 1, &dyn_offset);
      vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    }

    used_graphics_ = true;
    return Status::Ok();
  }

  Status EndShadowPass() override {
    if (!shadow_pass_active_) {
      return Status::Fail("BeginShadowPass not active");
    }
    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdEndRenderPass(cmd);
    shadow_pass_active_ = false;
    BarrierShadowImage(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    bound_cascade_ = -1;
    return Status::Ok();
  }
  Status BeginLocalShadowPass() override { return Status::Fail("Vulkan lit not ready"); }
  Status BindLocalShadowTile(int) override { return Status::Fail("Vulkan lit not ready"); }
  Status EndLocalShadowPass() override { return Status::Fail("Vulkan lit not ready"); }
  Status SetupPostMesh(const PostShaders&) override { return Status::Fail("Vulkan lit not ready"); }
  Status ResolvePostEffects(const PostResolveDesc&) override {
    return Status::Fail("Vulkan lit not ready");
  }

  Status DrawLitCube(const LitDrawItem& item) override {
    return DrawLitCubes(std::span<const LitDrawItem>(&item, 1));
  }

  Status DrawTransparentLitCubes(std::span<const LitDrawItem> items) override {
    // No transparent lit PSO yet; reuse opaque lit path when lit is ready.
    return DrawLitCubes(items);
  }

  Status DrawLitCubes(std::span<const LitDrawItem> items) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (items.empty()) {
      return Status::Ok();
    }
    if (!pass_active_) {
      if (auto st = BeginLitRenderPass(clear_color_); !st) {
        return st;
      }
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_);

    VkViewport vp{};
    vp.width = static_cast<float>(width_);
    vp.height = static_cast<float>(height_);
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const VkDeviceSize vb_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &cube_vb_, &vb_offset);
    vkCmdBindIndexBuffer(cmd, cube_ib_, 0, VK_INDEX_TYPE_UINT16);

    for (std::size_t i = 0; i < items.size(); ++i) {
      if (lit_draws_this_frame_ >= kMaxLitDraws) {
        break;
      }
      ObjectGpu od{};
      std::memcpy(od.world, items[i].world.m.data(), sizeof(od.world));
      od.color[0] = items[i].color.r;
      od.color[1] = items[i].color.g;
      od.color[2] = items[i].color.b;
      od.color[3] = items[i].color.a;
      od.metallic = items[i].metallic;
      od.roughness = items[i].roughness;

      const VkDeviceSize slot = static_cast<VkDeviceSize>(lit_draws_this_frame_) * kUniformAlign;
      void* mapped = nullptr;
      if (vkMapMemory(device_, object_ub_mem_, slot, sizeof(od), 0, &mapped) != VK_SUCCESS) {
        return Status::Fail("Map object UB failed");
      }
      std::memcpy(mapped, &od, sizeof(od));
      vkUnmapMemory(device_, object_ub_mem_);

      const std::uint32_t dyn_offset = static_cast<std::uint32_t>(slot);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_layout_, 0, 1,
                              &lit_desc_set_, 1, &dyn_offset);
      vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
      ++lit_draws_this_frame_;
    }

    used_graphics_ = true;
    return Status::Ok();
  }

  Status UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height,
                             int slot) override {
    (void)rgba;
    (void)width;
    (void)height;
    (void)slot;
    return Status::Fail("Vulkan lit not ready");
  }
  Status UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height, int slot) override {
    (void)rgba;
    (void)width;
    (void)height;
    (void)slot;
    return Status::Fail("Vulkan lit not ready");
  }
  Status UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                           std::span<const std::uint32_t> indices) override {
    (void)mesh_slot;
    (void)vertices;
    (void)indices;
    return Status::Fail("Vulkan lit geometry not ready");
  }
  Status DrawScreenQuads(std::span<const ScreenQuad>) override {
    return Status::Fail("Vulkan lit not ready");
  }
  Status DrawDebugLines(std::span<const DebugLineVertex>) override {
    return Status::Ok();  // no-op until Vulkan debug path
  }
  Status SetupUiMesh(const SimpleMeshShaders&) override {
    return Status::Fail("Vulkan lit not ready");
  }
  Status UploadUiFontAtlas(const std::uint8_t*, int, int) override {
    return Status::Fail("Vulkan lit not ready");
  }
  Status DrawUiMesh(std::span<const UiVertex>, std::span<const std::uint16_t>,
                    std::span<const UiDrawCmd>) override {
    return Status::Fail("Vulkan lit not ready");
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
  Status BeginLitRenderPass(const ColorRgba& color) {
    if (pass_active_) {
      cleared_ = true;
      used_graphics_ = true;
      return Status::Ok();
    }
    if (framebuffers_.empty() || image_index_ >= framebuffers_.size()) {
      return Status::Fail("Lit framebuffers missing");
    }

    VkClearValue clears[2]{};
    clears[0].color = {{color.r, color.g, color.b, color.a}};
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = render_pass_;
    rp.framebuffer = framebuffers_[image_index_];
    rp.renderArea.extent = {width_, height_};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    vkCmdBeginRenderPass(command_buffers_[frame_index_], &rp, VK_SUBPASS_CONTENTS_INLINE);
    pass_active_ = true;
    cleared_ = true;
    used_graphics_ = true;
    return Status::Ok();
  }

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
    vkGetPhysicalDeviceMemoryProperties(physical_, &mem_props_);
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

  void DestroySwapchainViews() {
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

  void DestroySwapchain() {
    DestroyFramebuffersOnly();
    DestroyDepthOnly();
    DestroySwapchainViews();
  }

  Status RecreateSwapchain() {
    if (device_ == VK_NULL_HANDLE) {
      return Status::Fail("No device");
    }
    vkDeviceWaitIdle(device_);
    DestroyFramebuffersOnly();
    DestroyDepthOnly();
    DestroySwapchainViews();
    if (auto st = CreateSwapchain(); !st) {
      return st;
    }
    if (lit_ready_) {
      if (auto st = CreateDepthResources(); !st) {
        return st;
      }
      if (auto st = CreateFramebuffers(); !st) {
        return st;
      }
    }
    return Status::Ok();
  }

  std::uint32_t FindMemoryType(std::uint32_t type_bits, VkMemoryPropertyFlags props) const {
    for (std::uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
      if ((type_bits & (1u << i)) &&
          (mem_props_.memoryTypes[i].propertyFlags & props) == props) {
        return i;
      }
    }
    return UINT32_MAX;
  }

  Status CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                      VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &buffer) != VK_SUCCESS) {
      return Status::Fail("vkCreateBuffer failed");
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, buffer, &req);
    const std::uint32_t type = FindMemoryType(req.memoryTypeBits, props);
    if (type == UINT32_MAX) {
      return Status::Fail("No suitable memory type for buffer");
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &ai, nullptr, &memory) != VK_SUCCESS) {
      return Status::Fail("vkAllocateMemory failed");
    }
    vkBindBufferMemory(device_, buffer, memory, 0);
    return Status::Ok();
  }

  Status CreateImage(std::uint32_t width, std::uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {width, height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &image) != VK_SUCCESS) {
      return Status::Fail("vkCreateImage failed");
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, image, &req);
    const std::uint32_t type =
        FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
      return Status::Fail("No device-local memory for image");
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &ai, nullptr, &memory) != VK_SUCCESS) {
      return Status::Fail("vkAllocateMemory for image failed");
    }
    vkBindImageMemory(device_, image, memory, 0);
    return Status::Ok();
  }

  void BarrierShadowImage(VkCommandBuffer cmd, VkImageLayout old_layout,
                          VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadow_image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
      src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
      src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
      src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = 0;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    shadow_layout_ = new_layout;
  }

  Status ImmediateTransitionShadow(VkImageLayout new_layout) {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &alloc, &cmd) != VK_SUCCESS) {
      return Status::Fail("Allocate transition cmd failed");
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    BarrierShadowImage(cmd, shadow_layout_, new_layout);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
    return Status::Ok();
  }

  Status CreateRenderPass() {
    if (render_pass_ != VK_NULL_HANDLE) {
      return Status::Ok();
    }
    VkAttachmentDescription color{};
    color.format = surface_format_.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = dep.srcStageMask;
    dep.srcAccessMask = 0;
    dep.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const VkAttachmentDescription atts[] = {color, depth};
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &render_pass_) != VK_SUCCESS) {
      return Status::Fail("vkCreateRenderPass failed");
    }
    return Status::Ok();
  }

  void DestroyDepthOnly() {
    if (depth_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, depth_view_, nullptr);
      depth_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, depth_image_, nullptr);
      depth_image_ = VK_NULL_HANDLE;
    }
    if (depth_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, depth_mem_, nullptr);
      depth_mem_ = VK_NULL_HANDLE;
    }
  }

  Status CreateDepthResources() {
    DestroyDepthOnly();
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_D32_SFLOAT;
    ii.extent = {width_, height_, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &depth_image_) != VK_SUCCESS) {
      return Status::Fail("Create depth image failed");
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, depth_image_, &req);
    const std::uint32_t type =
        FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
      return Status::Fail("No device-local memory for depth");
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &ai, nullptr, &depth_mem_) != VK_SUCCESS) {
      return Status::Fail("Allocate depth memory failed");
    }
    vkBindImageMemory(device_, depth_image_, depth_mem_, 0);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = depth_image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vi, nullptr, &depth_view_) != VK_SUCCESS) {
      return Status::Fail("Create depth view failed");
    }
    return Status::Ok();
  }

  void DestroyFramebuffersOnly() {
    for (VkFramebuffer fb : framebuffers_) {
      if (fb != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, fb, nullptr);
      }
    }
    framebuffers_.clear();
  }

  Status CreateFramebuffers() {
    DestroyFramebuffersOnly();
    framebuffers_.resize(swapchain_views_.size());
    for (std::size_t i = 0; i < swapchain_views_.size(); ++i) {
      const VkImageView atts[] = {swapchain_views_[i], depth_view_};
      VkFramebufferCreateInfo fi{};
      fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fi.renderPass = render_pass_;
      fi.attachmentCount = 2;
      fi.pAttachments = atts;
      fi.width = width_;
      fi.height = height_;
      fi.layers = 1;
      if (vkCreateFramebuffer(device_, &fi, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
        return Status::Fail("vkCreateFramebuffer failed");
      }
    }
    return Status::Ok();
  }

  Status CreateShaderModule(const std::vector<std::uint8_t>& spirv, VkShaderModule& out) {
    if (spirv.size() % 4 != 0) {
      return Status::Fail("SPIR-V size not multiple of 4");
    }
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size();
    ci.pCode = reinterpret_cast<const std::uint32_t*>(spirv.data());
    if (vkCreateShaderModule(device_, &ci, nullptr, &out) != VK_SUCCESS) {
      return Status::Fail("vkCreateShaderModule failed");
    }
    return Status::Ok();
  }

  Status CreateLitPipeline(const std::vector<std::uint8_t>& vs_spv,
                           const std::vector<std::uint8_t>& ps_spv) {
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule ps = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(vs_spv, vs); !st) {
      return st;
    }
    if (auto st = CreateShaderModule(ps_spv, ps); !st) {
      vkDestroyShaderModule(device_, vs, nullptr);
      return st;
    }

    VkDescriptorSetLayoutBinding binds[3]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[2].binding = 2;
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 3;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &lit_set_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs, nullptr);
      vkDestroyShaderModule(device_, ps, nullptr);
      return Status::Fail("vkCreateDescriptorSetLayout failed");
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &lit_set_layout_;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &lit_pipeline_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs, nullptr);
      vkDestroyShaderModule(device_, ps, nullptr);
      return Status::Fail("vkCreatePipelineLayout failed");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "VSMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName = "PSMain";

    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(LitVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LitVertex, px)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LitVertex, nx)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LitVertex, u)};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;  // D3D/HLSL clip-space Y-down style via DXC
    rs.lineWidth = 1.f;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_att;

    const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &blend;
    gp.pDynamicState = &dyn;
    gp.layout = lit_pipeline_layout_;
    gp.renderPass = render_pass_;
    gp.subpass = 0;

    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                                 &lit_pipeline_);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, ps, nullptr);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkCreateGraphicsPipelines failed: " + VkErr(r));
    }
    return Status::Ok();
  }

  Status CreateShadowResources(const std::vector<std::uint8_t>& shadow_vs_spv) {
    if (auto st = CreateImage(kShadowMapSize, kShadowMapSize, VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT,
                              shadow_image_, shadow_mem_);
        !st) {
      return st;
    }

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = shadow_image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_D32_SFLOAT;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vi, nullptr, &shadow_view_) != VK_SUCCESS) {
      return Status::Fail("Create shadow view failed");
    }

    shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (auto st = ImmediateTransitionShadow(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        !st) {
      return st;
    }

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &depth;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &shadow_render_pass_) != VK_SUCCESS) {
      return Status::Fail("Create shadow render pass failed");
    }

    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = shadow_render_pass_;
    fi.attachmentCount = 1;
    fi.pAttachments = &shadow_view_;
    fi.width = kShadowMapSize;
    fi.height = kShadowMapSize;
    fi.layers = 1;
    if (vkCreateFramebuffer(device_, &fi, nullptr, &shadow_framebuffer_) != VK_SUCCESS) {
      return Status::Fail("Create shadow framebuffer failed");
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.compareEnable = VK_TRUE;
    sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (vkCreateSampler(device_, &sci, nullptr, &shadow_sampler_) != VK_SUCCESS) {
      return Status::Fail("Create shadow comparison sampler failed");
    }

    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 2;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &shadow_set_layout_) != VK_SUCCESS) {
      return Status::Fail("Create shadow set layout failed");
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &shadow_set_layout_;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &shadow_pipeline_layout_) != VK_SUCCESS) {
      return Status::Fail("Create shadow pipeline layout failed");
    }

    VkShaderModule vs = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(shadow_vs_spv, vs); !st) {
      return st;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vs;
    stage.pName = "ShadowVS";

    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(LitVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = offsetof(LitVertex, px);

    VkPipelineVertexInputStateCreateInfo vi_state{};
    vi_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi_state.vertexBindingDescriptionCount = 1;
    vi_state.pVertexBindingDescriptions = &bind;
    vi_state.vertexAttributeDescriptionCount = 1;
    vi_state.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.f;
    rs.rasterizerDiscardEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 0;

    const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 1;
    gp.pStages = &stage;
    gp.pVertexInputState = &vi_state;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &blend;
    gp.pDynamicState = &dyn;
    gp.layout = shadow_pipeline_layout_;
    gp.renderPass = shadow_render_pass_;
    gp.subpass = 0;

    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                                 &shadow_pipeline_);
    vkDestroyShaderModule(device_, vs, nullptr);
    if (r != VK_SUCCESS) {
      return Status::Fail("Create shadow pipeline failed: " + VkErr(r));
    }
    return Status::Ok();
  }

  Status CreateCubeMesh() {
    const LitVertex verts[] = {
        {-0.5f, -0.5f, 0.5f, 0, 0, 1, 0, 0},  {0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 0},
        {0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 1},    {-0.5f, 0.5f, 0.5f, 0, 0, 1, 0, 1},
        {0.5f, -0.5f, -0.5f, 0, 0, -1, 0, 0}, {-0.5f, -0.5f, -0.5f, 0, 0, -1, 1, 0},
        {-0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 1}, {0.5f, 0.5f, -0.5f, 0, 0, -1, 0, 1},
        {0.5f, -0.5f, 0.5f, 1, 0, 0, 0, 0},   {0.5f, -0.5f, -0.5f, 1, 0, 0, 1, 0},
        {0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 1},   {0.5f, 0.5f, 0.5f, 1, 0, 0, 0, 1},
        {-0.5f, -0.5f, -0.5f, -1, 0, 0, 0, 0},{-0.5f, -0.5f, 0.5f, -1, 0, 0, 1, 0},
        {-0.5f, 0.5f, 0.5f, -1, 0, 0, 1, 1},  {-0.5f, 0.5f, -0.5f, -1, 0, 0, 0, 1},
        {-0.5f, 0.5f, 0.5f, 0, 1, 0, 0, 0},   {0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 0},
        {0.5f, 0.5f, -0.5f, 0, 1, 0, 1, 1},   {-0.5f, 0.5f, -0.5f, 0, 1, 0, 0, 1},
        {-0.5f, -0.5f, -0.5f, 0, -1, 0, 0, 0},{0.5f, -0.5f, -0.5f, 0, -1, 0, 1, 0},
        {0.5f, -0.5f, 0.5f, 0, -1, 0, 1, 1},  {-0.5f, -0.5f, 0.5f, 0, -1, 0, 0, 1},
    };
    const std::uint16_t indices[] = {
        0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
        12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (auto st = CreateBuffer(sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, cube_vb_,
                               cube_vb_mem_);
        !st) {
      return st;
    }
    if (auto st = CreateBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host, cube_ib_,
                               cube_ib_mem_);
        !st) {
      return st;
    }
    void* mapped = nullptr;
    vkMapMemory(device_, cube_vb_mem_, 0, sizeof(verts), 0, &mapped);
    std::memcpy(mapped, verts, sizeof(verts));
    vkUnmapMemory(device_, cube_vb_mem_);
    vkMapMemory(device_, cube_ib_mem_, 0, sizeof(indices), 0, &mapped);
    std::memcpy(mapped, indices, sizeof(indices));
    vkUnmapMemory(device_, cube_ib_mem_);
    return Status::Ok();
  }

  Status CreateLitBuffersAndDescriptors() {
    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (auto st = CreateBuffer(kFrameUbSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, frame_ub_,
                               frame_ub_mem_);
        !st) {
      return st;
    }
    if (auto st = CreateBuffer(kUniformAlign, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host,
                               shadow_frame_ub_, shadow_frame_ub_mem_);
        !st) {
      return st;
    }
    if (auto st =
            CreateBuffer(kUniformAlign * kMaxLitDraws, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host,
                         object_ub_, object_ub_mem_);
        !st) {
      return st;
    }

    VkDescriptorPoolSize sizes[3]{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2};
    sizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2};
    sizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 2;
    pci.poolSizeCount = 3;
    pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device_, &pci, nullptr, &lit_desc_pool_) != VK_SUCCESS) {
      return Status::Fail("vkCreateDescriptorPool failed");
    }

    {
      VkDescriptorSetAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool = lit_desc_pool_;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &lit_set_layout_;
      if (vkAllocateDescriptorSets(device_, &ai, &lit_desc_set_) != VK_SUCCESS) {
        return Status::Fail("vkAllocateDescriptorSets (lit) failed");
      }
    }
    {
      VkDescriptorSetAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool = lit_desc_pool_;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &shadow_set_layout_;
      if (vkAllocateDescriptorSets(device_, &ai, &shadow_desc_set_) != VK_SUCCESS) {
        return Status::Fail("vkAllocateDescriptorSets (shadow) failed");
      }
    }

    VkDescriptorBufferInfo frame_info{};
    frame_info.buffer = frame_ub_;
    frame_info.offset = 0;
    frame_info.range = sizeof(FrameGpu);

    VkDescriptorBufferInfo obj_info{};
    obj_info.buffer = object_ub_;
    obj_info.offset = 0;
    obj_info.range = sizeof(ObjectGpu);

    VkDescriptorImageInfo shadow_info{};
    shadow_info.sampler = shadow_sampler_;
    shadow_info.imageView = shadow_view_;
    shadow_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo shadow_frame_info{};
    shadow_frame_info.buffer = shadow_frame_ub_;
    shadow_frame_info.offset = 0;
    shadow_frame_info.range = sizeof(ShadowFrameGpu);

    VkWriteDescriptorSet writes[5]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = lit_desc_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &frame_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = lit_desc_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[1].pBufferInfo = &obj_info;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = lit_desc_set_;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &shadow_info;
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = shadow_desc_set_;
    writes[3].dstBinding = 0;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].pBufferInfo = &shadow_frame_info;
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = shadow_desc_set_;
    writes[4].dstBinding = 1;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[4].pBufferInfo = &obj_info;
    vkUpdateDescriptorSets(device_, 5, writes, 0, nullptr);
    return Status::Ok();
  }

  void DestroyLitResources() {
    lit_ready_ = false;
    shadow_pass_active_ = false;
    bound_cascade_ = -1;
    DestroyFramebuffersOnly();
    DestroyDepthOnly();

    if (shadow_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, shadow_pipeline_, nullptr);
      shadow_pipeline_ = VK_NULL_HANDLE;
    }
    if (shadow_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, shadow_pipeline_layout_, nullptr);
      shadow_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (shadow_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, shadow_set_layout_, nullptr);
      shadow_set_layout_ = VK_NULL_HANDLE;
    }
    if (shadow_framebuffer_ != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, shadow_framebuffer_, nullptr);
      shadow_framebuffer_ = VK_NULL_HANDLE;
    }
    if (shadow_render_pass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, shadow_render_pass_, nullptr);
      shadow_render_pass_ = VK_NULL_HANDLE;
    }
    if (shadow_sampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, shadow_sampler_, nullptr);
      shadow_sampler_ = VK_NULL_HANDLE;
    }
    if (shadow_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, shadow_view_, nullptr);
      shadow_view_ = VK_NULL_HANDLE;
    }
    if (shadow_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, shadow_image_, nullptr);
      shadow_image_ = VK_NULL_HANDLE;
    }
    if (shadow_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, shadow_mem_, nullptr);
      shadow_mem_ = VK_NULL_HANDLE;
    }
    shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    shadow_desc_set_ = VK_NULL_HANDLE;

    if (lit_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, lit_pipeline_, nullptr);
      lit_pipeline_ = VK_NULL_HANDLE;
    }
    if (lit_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, lit_pipeline_layout_, nullptr);
      lit_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (lit_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, lit_set_layout_, nullptr);
      lit_set_layout_ = VK_NULL_HANDLE;
    }
    if (lit_desc_pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, lit_desc_pool_, nullptr);
      lit_desc_pool_ = VK_NULL_HANDLE;
      lit_desc_set_ = VK_NULL_HANDLE;
    }
    auto destroy_buf = [&](VkBuffer& b, VkDeviceMemory& m) {
      if (b != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, b, nullptr);
        b = VK_NULL_HANDLE;
      }
      if (m != VK_NULL_HANDLE) {
        vkFreeMemory(device_, m, nullptr);
        m = VK_NULL_HANDLE;
      }
    };
    destroy_buf(cube_vb_, cube_vb_mem_);
    destroy_buf(cube_ib_, cube_ib_mem_);
    destroy_buf(frame_ub_, frame_ub_mem_);
    destroy_buf(shadow_frame_ub_, shadow_frame_ub_mem_);
    destroy_buf(object_ub_, object_ub_mem_);

    if (render_pass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, render_pass_, nullptr);
      render_pass_ = VK_NULL_HANDLE;
    }
  }

  HWND hwnd_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mem_props_{};
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
  bool pass_active_ = false;
  bool used_graphics_ = false;
  ColorRgba clear_color_{0.f, 0.f, 0.f, 1.f};

  bool lit_ready_ = false;
  bool shadow_pass_active_ = false;
  int bound_cascade_ = -1;
  std::uint32_t lit_draws_this_frame_ = 0;
  FrameLighting lighting_{};

  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkImage depth_image_ = VK_NULL_HANDLE;
  VkDeviceMemory depth_mem_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers_;

  VkDescriptorSetLayout lit_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout lit_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline lit_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool lit_desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet lit_desc_set_ = VK_NULL_HANDLE;

  VkImage shadow_image_ = VK_NULL_HANDLE;
  VkDeviceMemory shadow_mem_ = VK_NULL_HANDLE;
  VkImageView shadow_view_ = VK_NULL_HANDLE;
  VkSampler shadow_sampler_ = VK_NULL_HANDLE;
  VkRenderPass shadow_render_pass_ = VK_NULL_HANDLE;
  VkFramebuffer shadow_framebuffer_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout shadow_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout shadow_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorSet shadow_desc_set_ = VK_NULL_HANDLE;
  VkImageLayout shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

  VkBuffer cube_vb_ = VK_NULL_HANDLE;
  VkDeviceMemory cube_vb_mem_ = VK_NULL_HANDLE;
  VkBuffer cube_ib_ = VK_NULL_HANDLE;
  VkDeviceMemory cube_ib_mem_ = VK_NULL_HANDLE;
  VkBuffer frame_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory frame_ub_mem_ = VK_NULL_HANDLE;
  VkBuffer shadow_frame_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory shadow_frame_ub_mem_ = VK_NULL_HANDLE;
  VkBuffer object_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory object_ub_mem_ = VK_NULL_HANDLE;
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
