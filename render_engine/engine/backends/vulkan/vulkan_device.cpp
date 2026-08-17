#include "engine/rhi/i_device.h"

#include "engine/core/feature.h"
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
#include <cstdlib>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace engine::rhi {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
// Match D3D12: 4 CSM cascades + local-shadow tiles share one UB ring.
constexpr std::uint32_t kShadowVpSlots = 16;
// Opaque scene + CPU-expanded scale instances (Sandbox uses up to 1024).
constexpr std::uint32_t kMaxLitDraws = 2048;
constexpr VkDeviceSize kUniformAlign = 256;
constexpr std::uint32_t kShadowMapSize = 2048;
constexpr std::uint32_t kLocalShadowMapSize = 2048;
constexpr int kMaxMeshSlots = 8;
constexpr std::uint32_t kMaxScreenQuads = 1024;
constexpr std::uint32_t kMaxDebugVerts = 16384;
constexpr std::uint32_t kMaxUiVerts = 16384;
constexpr std::uint32_t kMaxUiIndices = 49152;
static constexpr VkFormat kHdrColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

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
  float enable_ibl;
  float ibl_intensity;
  float enable_reflection;
  float reflection_intensity;
  float local_count;
  float enable_taa;
  float _pad_before_lights[2];
  float local_pos_range[16][4];
  float local_color_intensity[16][4];
  float local_spot[16][4];     // xyz=dir, w=cosOuter (-1 = point/omni)
  float local_spot_inner[16];  // cosInner for lights 0..15
  float local_shadow_vp[12][16];
  float enable_local_shadow;
  float local_shadow_bias;
  float local_shadow_count;
  float local_shadow_tiles;
  float prev_view_proj[16];
  float jitter_x;
  float jitter_y;
  float _pad_jitter[2];
  float local_ies[16];  // C03/W7
  // Mega-W8 C02: packed Forward+ tile lists (must match lit_cube_vk.hlsl).
  float enable_tiled_lights;
  float tile_grid_w;
  float tile_grid_h;
  float max_lights_per_tile;
  float tile_light_count[32];
  float tile_light_index[256];
};

struct ShadowFrameGpu {
  float view_proj[16];
};

struct ObjectGpu {
  float world[16];
  float color[4];
  float metallic;
  float roughness;
  float use_albedo;
  float use_orm;
  float tex_slot;
  float uv_scale;
  float use_instances;
  float pad;
};

struct MeshSlotGpu {
  VkBuffer vb = VK_NULL_HANDLE;
  VkBuffer ib = VK_NULL_HANDLE;
  VkDeviceMemory vb_mem = VK_NULL_HANDLE;
  VkDeviceMemory ib_mem = VK_NULL_HANDLE;
  std::uint32_t index_count = 0;
  VkIndexType index_type = VK_INDEX_TYPE_UINT32;
};

struct Tex2DGpu {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

constexpr VkDeviceSize kFrameUbSize =
    (sizeof(FrameGpu) + kUniformAlign - 1) / kUniformAlign * kUniformAlign;

// Must match post_ssao_taa_vk.hlsl / D3D12 ResolvePostEffects packing.
struct PostCB {
  float inv_res[2];
  float enable_ssao;
  float enable_taa;
  float ssao_radius;
  float ssao_intensity;
  float taa_blend;
  float exposure;
  float inv_view_proj[16];
  float view_proj[16];
  float eye[3];
  float tonemap_mode;
  float enable_auto_exposure;
  float auto_exposure_key;
  float enable_bloom;
  float bloom_threshold;
  float bloom_intensity;
  float enable_fog;
  float fog_density;
  float fog_start;
  float fog_color[3];
  float enable_tonemap;
  float enable_ssr;
  float ssr_intensity;
  float ssr_thickness;
  float enable_dof;
  float dof_focus;
  float dof_scale;
  float enable_motion_blur;
  float motion_blur_strength;
  float prev_view_proj[16];
  float jitter_x;
  float jitter_y;
  float vignette_strength;
  float film_grain_strength;
  float chromatic_aberration;
  float lens_distortion;
  float light_dirt_strength;
  float flare_strength;
  float pad_c04_a;
  float pad_c04_b;
};
static_assert(sizeof(PostCB) <= 512, "post CB exceeds upload buffer");
constexpr VkDeviceSize kPostUbSize =
    (sizeof(PostCB) + kUniformAlign - 1) / kUniformAlign * kUniformAlign;

// Screen passes: D3D clip Y-up + Vulkan negative viewport height → upright FB.
// Shadow atlas uses the same Y-flip so cascade depth matches D3D UV (proj.y * -0.5).
// Positive-height shadow VP previously inverted V vs D3D → SampleCmp missed casters.
// Neg-height flips winding in FB → lit uses FRONT_FACE_CLOCKWISE with CCW meshes.
inline VkViewport MakeViewport(float x, float y, float w, float h) {
  VkViewport vp{};
  vp.x = x;
  vp.y = y;
  vp.width = w;
  vp.height = h;
  vp.minDepth = 0.f;
  vp.maxDepth = 1.f;
  return vp;
}

inline VkViewport MakeYFlippedViewport(float x, float y, float w, float h) {
  VkViewport vp{};
  vp.x = x;
  vp.y = y + h;
  vp.width = w;
  vp.height = -h;
  vp.minDepth = 0.f;
  vp.maxDepth = 1.f;
  return vp;
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
    adapter_index_ = desc.adapter_index;
    enable_validation_ = desc.enable_validation;
    vsync_ = desc.enable_vsync;
    if (const char* v = std::getenv("ENGINE_ENABLE_VALIDATION"); v && v[0] == '1') {
      enable_validation_ = true;
    }

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

    // No descriptor-indexing / bindless albedo path on VK yet — do not fake Feature.
    // Capability "bindless" and optional "bindless_hot_path" stay unset; Probe fails.
    LogInfo("Vulkan bindless SKIP (no descriptor-indexing albedo path; classic descriptors only)");
    LogInfo("Vulkan device ready (Win32 surface + swapchain clear)");
    return Status::Ok();
  }

  ~VulkanDevice() override {
    if (device_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
    }
    DestroyLitResources();
    if (color_readback_buf_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, color_readback_buf_, nullptr);
      color_readback_buf_ = VK_NULL_HANDLE;
    }
    if (color_readback_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, color_readback_mem_, nullptr);
      color_readback_mem_ = VK_NULL_HANDLE;
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

  void SetVSync(bool enabled) override {
    if (vsync_ == enabled) {
      return;
    }
    vsync_ = enabled;
    vsync_dirty_ = true;
  }
  [[nodiscard]] bool vsync() const override { return vsync_; }

  Status BeginFrame() override {
    if (vsync_dirty_ && device_ != VK_NULL_HANDLE && swapchain_ != VK_NULL_HANDLE) {
      vsync_dirty_ = false;
      if (auto st = RecreateSwapchain(); !st) {
        LogWarn(std::string("Vulkan vsync swapchain recreate failed: ") + st.message());
      } else {
        LogInfo(std::string("Vulkan vsync=") + (vsync_ ? "on" : "off"));
      }
    }
    // Shadow atlas + lit UBs are shared across frames-in-flight. Wait for every
    // slot before recording so a previous frame cannot sample while this frame
    // clears the atlas (low-frequency flicker with Shadows on).
    VkResult r = vkWaitForFences(device_, kFramesInFlight, in_flight_fences_.data(), VK_TRUE,
                                 UINT64_MAX);
    if (r != VK_SUCCESS) {
      return Status::Fail("vkWaitForFences failed: " + VkErr(r));
    }
    VkFence fence = in_flight_fences_[frame_index_];

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
    present_pass_active_ = false;
    present_pass_load_ = false;
    shadow_pass_active_ = false;
    post_resolved_this_frame_ = false;
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
    // Ensure HDR targets are cleared when nothing drew this frame. Do not re-open HDR after post.
    if (lit_ready_ && !pass_active_ && !post_resolved_this_frame_) {
      if (auto st = BeginLitRenderPass(clear_color_); !st) {
        return st;
      }
    }
    const bool need_swapchain_present_barrier =
        pass_active_ && present_pass_active_ && !present_pass_load_;
    if (pass_active_) {
      vkCmdEndRenderPass(cmd);
      pass_active_ = false;
      present_pass_active_ = false;
      present_pass_load_ = false;
    }
    // Present pass (clear color) finalLayout is COLOR_ATTACHMENT. Move swapchain to PRESENT.
    if (need_swapchain_present_barrier) {
      VkImageMemoryBarrier to_present{};
      to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      to_present.dstAccessMask = 0;
      to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_present.image = swapchain_images_[image_index_];
      to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      to_present.subresourceRange.levelCount = 1;
      to_present.subresourceRange.layerCount = 1;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &to_present);
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

    if (!shaders.quad_vs_dxil.empty() && !shaders.quad_ps_dxil.empty()) {
      if (auto st = SetupScreenQuads(shaders.quad_vs_dxil, shaders.quad_ps_dxil); !st) {
        return st;
      }
    }
    if (!shaders.debug_vs_dxil.empty() && !shaders.debug_ps_dxil.empty()) {
      if (auto st = SetupDebugLines(shaders.debug_vs_dxil, shaders.debug_ps_dxil); !st) {
        return st;
      }
    }

    lit_ready_ = true;
    LogInfo("Vulkan lit cube ready (depth + CSM shadows + mesh slots)");
    return Status::Ok();
  }

  Status SetFrameLighting(const FrameLighting& lighting) override {
    if (!lit_ready_ || !frame_ub_) {
      return Status::Fail("SetupLitMesh not called");
    }
    // Mat4::Perspective already outputs clip Z in [0,1] for D3D/Vulkan.
    lighting_ = lighting;

    FrameGpu data{};
    std::memcpy(data.view_proj, lighting_.view_proj.m.data(), sizeof(data.view_proj));
    for (int i = 0; i < 4; ++i) {
      std::memcpy(data.cascade_vp[i], lighting_.cascade_view_proj[static_cast<std::size_t>(i)].m.data(),
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
    data.enable_ibl = lighting_.enable_ibl ? 1.f : 0.f;
    data.ibl_intensity = lighting_.ibl_intensity;
    data.enable_reflection = lighting_.enable_reflection_probe ? 1.f : 0.f;
    data.reflection_intensity = lighting_.reflection_intensity;
    data.local_count = static_cast<float>(lighting_.local_light_count);
    data.enable_taa = lighting_.enable_taa ? 1.f : 0.f;
    for (int i = 0; i < 16; ++i) {
      data.local_pos_range[i][0] = lighting_.local_pos[static_cast<std::size_t>(i)].x;
      data.local_pos_range[i][1] = lighting_.local_pos[static_cast<std::size_t>(i)].y;
      data.local_pos_range[i][2] = lighting_.local_pos[static_cast<std::size_t>(i)].z;
      data.local_pos_range[i][3] = lighting_.local_range[static_cast<std::size_t>(i)];
      data.local_color_intensity[i][0] = lighting_.local_color[static_cast<std::size_t>(i)].r;
      data.local_color_intensity[i][1] = lighting_.local_color[static_cast<std::size_t>(i)].g;
      data.local_color_intensity[i][2] = lighting_.local_color[static_cast<std::size_t>(i)].b;
      data.local_color_intensity[i][3] = lighting_.local_intensity[static_cast<std::size_t>(i)];
      data.local_spot[i][0] = lighting_.local_spot[static_cast<std::size_t>(i)].x;
      data.local_spot[i][1] = lighting_.local_spot[static_cast<std::size_t>(i)].y;
      data.local_spot[i][2] = lighting_.local_spot[static_cast<std::size_t>(i)].z;
      data.local_spot[i][3] = lighting_.local_spot[static_cast<std::size_t>(i)].w;
      data.local_spot_inner[i] = lighting_.local_spot_inner[static_cast<std::size_t>(i)];
      data.local_ies[i] = lighting_.local_ies[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < 12; ++i) {
      std::memcpy(data.local_shadow_vp[i],
                  lighting_.local_shadow_vps[static_cast<std::size_t>(i)].m.data(),
                  sizeof(data.local_shadow_vp[i]));
    }
    std::memcpy(data.local_shadow_vp[0], lighting_.local_shadow_vp.m.data(),
                sizeof(data.local_shadow_vp[0]));
    data.enable_local_shadow =
        (lighting_.enable_local_shadow && local_shadow_image_ != VK_NULL_HANDLE) ? 1.f : 0.f;
    data.local_shadow_bias = lighting_.local_shadow_bias;
    data.local_shadow_count = static_cast<float>(lighting_.local_shadow_count);
    data.local_shadow_tiles =
        static_cast<float>((std::max)(1, lighting_.local_shadow_tiles_per_row));
    std::memcpy(data.prev_view_proj, lighting_.prev_view_proj.m.data(),
                sizeof(data.prev_view_proj));
    data.jitter_x = lighting_.jitter_x;
    data.jitter_y = lighting_.jitter_y;
    data.enable_tiled_lights = lighting_.enable_tiled_lights ? 1.f : 0.f;
    data.tile_grid_w = 8.f;
    data.tile_grid_h = 4.f;
    data.max_lights_per_tile = 8.f;
    for (int i = 0; i < 32; ++i) {
      data.tile_light_count[i] =
          static_cast<float>(lighting_.tile_light_count[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < 256; ++i) {
      data.tile_light_index[i] =
          static_cast<float>(lighting_.tile_light_index[static_cast<std::size_t>(i)]);
    }

    const VkDeviceSize frame_off = static_cast<VkDeviceSize>(frame_index_) * kFrameUbSize;
    void* mapped = nullptr;
    if (vkMapMemory(device_, frame_ub_mem_, frame_off, sizeof(data), 0, &mapped) != VK_SUCCESS) {
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
    shadow_draws_this_pass_ = 0;
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
    // Per-cascade slot: must not overwrite other cascades before GPU executes.
    const VkDeviceSize frame_sh_off = ShadowVpUbOffset(cascade_index);
    void* mapped = nullptr;
    if (vkMapMemory(device_, shadow_frame_ub_mem_, frame_sh_off, sizeof(frame), 0, &mapped) !=
        VK_SUCCESS) {
      return Status::Fail("Map shadow frame UB failed");
    }
    std::memcpy(mapped, &frame, sizeof(frame));
    vkUnmapMemory(device_, shadow_frame_ub_mem_);

    const int tiles_per_row = (std::max)(1, lighting_.cascade_tiles_per_row);
    const float tile = static_cast<float>(kShadowMapSize) / static_cast<float>(tiles_per_row);
    const int ix = cascade_index % tiles_per_row;
    const int iy = cascade_index / tiles_per_row;

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    const float ox = static_cast<float>(ix) * tile;
    const float oy = static_cast<float>(iy) * tile;
    // Same D3D Y-up → texture V mapping as lit_cube_vk SampleCascadeShadow.
    VkViewport vp = MakeYFlippedViewport(ox, oy, tile, tile);
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset.x = static_cast<std::int32_t>(ox);
    scissor.offset.y = static_cast<std::int32_t>(oy);
    scissor.extent.width = static_cast<std::uint32_t>(tile);
    scissor.extent.height = static_cast<std::uint32_t>(tile);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    bound_cascade_ = cascade_index;
    bound_shadow_vp_slot_ = cascade_index;
    return Status::Ok();
  }

  Status DrawShadowCubes(std::span<const LitDrawItem> items) override {
    if (!lit_ready_ || (!shadow_pass_active_ && !local_shadow_pass_active_)) {
      return Status::Fail("BeginShadowPass/BeginLocalShadowPass not active");
    }
    if (items.empty()) {
      return Status::Ok();
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_);

    for (std::size_t i = 0; i < items.size(); ++i) {
      const int mesh_slot = items[i].mesh_slot;
      if (mesh_slot < 0 || mesh_slot >= kMaxMeshSlots ||
          mesh_slots_[mesh_slot].index_count == 0) {
        continue;
      }
      const MeshSlotGpu& mesh = mesh_slots_[mesh_slot];
      const VkDeviceSize vb_offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vb, &vb_offset);
      vkCmdBindIndexBuffer(cmd, mesh.ib, 0, mesh.index_type);

      ObjectGpu od{};
      std::memcpy(od.world, items[i].world.m.data(), sizeof(od.world));

      const std::uint32_t draw_slot = shadow_draws_this_pass_ % kMaxLitDraws;
      const VkDeviceSize slot =
          (static_cast<VkDeviceSize>(frame_index_) * kMaxLitDraws + draw_slot) * kUniformAlign;
      void* mapped = nullptr;
      if (vkMapMemory(device_, object_ub_mem_, slot, sizeof(od), 0, &mapped) != VK_SUCCESS) {
        return Status::Fail("Map object UB failed");
      }
      std::memcpy(mapped, &od, sizeof(od));
      vkUnmapMemory(device_, object_ub_mem_);

      const std::uint32_t dyn_offsets[2] = {
          static_cast<std::uint32_t>(ShadowVpUbOffset(bound_shadow_vp_slot_)),
          static_cast<std::uint32_t>(slot)};
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline_layout_, 0, 1,
                              &shadow_desc_set_, 2, dyn_offsets);
      vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
      ++shadow_draws_this_pass_;
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
  Status BeginLocalShadowPass() override {
    if (!lit_ready_ || local_shadow_image_ == VK_NULL_HANDLE) {
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
    if (shadow_pass_active_) {
      vkCmdEndRenderPass(cmd);
      shadow_pass_active_ = false;
      BarrierShadowImage(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    if (local_shadow_layout_ != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      BarrierLocalShadowImage(cmd, local_shadow_layout_,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    VkClearValue clear{};
    clear.depthStencil = {1.f, 0};
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = shadow_render_pass_;
    rp.framebuffer = local_shadow_framebuffer_;
    rp.renderArea.extent = {kLocalShadowMapSize, kLocalShadowMapSize};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    local_shadow_pass_active_ = true;
    shadow_draws_this_pass_ = 0;
    used_graphics_ = true;
    bound_shadow_vp_slot_ = 4;
    engine::SetFeatureOverride("local_shadow", true);
    return BindLocalShadowTile(0);
  }
  Status BindLocalShadowTile(int tile) override {
    if (!local_shadow_pass_active_) {
      return Status::Fail("BeginLocalShadowPass not active");
    }
    const int count = (std::max)(1, lighting_.local_shadow_tile_count > 0
                                        ? lighting_.local_shadow_tile_count
                                        : lighting_.local_shadow_count);
    if (tile < 0 || tile >= count) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid local shadow tile");
    }

    ShadowFrameGpu frame{};
    std::memcpy(frame.view_proj,
                lighting_.local_shadow_vps[static_cast<std::size_t>(tile)].m.data(),
                sizeof(frame.view_proj));
    if (tile == 0) {
      std::memcpy(frame.view_proj, lighting_.local_shadow_vp.m.data(), sizeof(frame.view_proj));
    }
    const int vp_slot = 4 + tile;
    const VkDeviceSize frame_sh_off = ShadowVpUbOffset(vp_slot);
    void* mapped = nullptr;
    if (vkMapMemory(device_, shadow_frame_ub_mem_, frame_sh_off, sizeof(frame), 0, &mapped) !=
        VK_SUCCESS) {
      return Status::Fail("Map shadow frame UB failed");
    }
    std::memcpy(mapped, &frame, sizeof(frame));
    vkUnmapMemory(device_, shadow_frame_ub_mem_);

    const int tiles_per_row = (std::max)(1, lighting_.local_shadow_tiles_per_row);
    const float tile_px =
        static_cast<float>(kLocalShadowMapSize) / static_cast<float>(tiles_per_row);
    const int ix = tile % tiles_per_row;
    const int iy = tile / tiles_per_row;
    const float ox = static_cast<float>(ix) * tile_px;
    const float oy = static_cast<float>(iy) * tile_px;

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    VkViewport vp = MakeYFlippedViewport(ox, oy, tile_px, tile_px);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{};
    scissor.offset.x = static_cast<std::int32_t>(ox);
    scissor.offset.y = static_cast<std::int32_t>(oy);
    scissor.extent.width = static_cast<std::uint32_t>(tile_px);
    scissor.extent.height = static_cast<std::uint32_t>(tile_px);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    bound_shadow_vp_slot_ = vp_slot;
    return Status::Ok();
  }
  Status EndLocalShadowPass() override {
    if (!local_shadow_pass_active_) {
      return Status::Ok();
    }
    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdEndRenderPass(cmd);
    local_shadow_pass_active_ = false;
    BarrierLocalShadowImage(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    if (lit_desc_set_ != VK_NULL_HANDLE && local_shadow_view_ != VK_NULL_HANDLE &&
        shadow_sampler_ != VK_NULL_HANDLE) {
      UpdateLitCombinedBinding(10, local_shadow_view_, shadow_sampler_,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
    lighting_.enable_local_shadow = true;
    engine::SetFeatureOverride("local_shadow", true);
    return Status::Ok();
  }

  Status UploadInstanceTransforms(std::span<const Mat4> worlds) override {
    instance_worlds_.assign(worlds.begin(), worlds.end());
    if (worlds.empty() || device_ == VK_NULL_HANDLE) {
      engine::SetFeatureOverride("gpu_instancing", true);
      return Status::Ok();
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(worlds.size() * sizeof(Mat4));
    auto& buf = instance_bufs_[frame_index_];
    auto& mem = instance_buf_mems_[frame_index_];
    auto& buf_bytes = instance_buf_bytes_[frame_index_];
    // BeginFrame waits all fences — safe to recreate this frame's slot.
    if (buf == VK_NULL_HANDLE || buf_bytes < bytes) {
      if (buf != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buf, nullptr);
        buf = VK_NULL_HANDLE;
      }
      if (mem != VK_NULL_HANDLE) {
        vkFreeMemory(device_, mem, nullptr);
        mem = VK_NULL_HANDLE;
      }
      const VkMemoryPropertyFlags host =
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      const VkDeviceSize alloc =
          (std::max)(bytes, static_cast<VkDeviceSize>(1024 * sizeof(Mat4)));
      if (auto st = CreateBuffer(alloc, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, buf, mem); !st) {
        return st;
      }
      buf_bytes = alloc;
    }
    void* mapped = nullptr;
    if (vkMapMemory(device_, mem, 0, bytes, 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map instance buffer failed");
    }
    std::memcpy(mapped, worlds.data(), static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, mem);
    UpdateLitInstanceBinding(buf, bytes);
    engine::SetFeatureOverride("gpu_instancing", true);
    return Status::Ok();
  }

  Status DrawLitInstanced(const LitDrawItem& prototype, std::uint32_t instance_count) override {
    if (!lit_ready_ || lit_pipeline_ == VK_NULL_HANDLE) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (instance_count == 0) {
      return Status::Ok();
    }
    VkBuffer ib = instance_bufs_[frame_index_];
    if (ib == VK_NULL_HANDLE || instance_worlds_.size() < instance_count) {
      return IDevice::DrawLitInstanced(prototype, instance_count);
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (!pass_active_) {
      if (auto st = BeginLitRenderPass(clear_color_); !st) {
        return st;
      }
    }

    const int mesh_slot = prototype.mesh_slot;
    if (mesh_slot < 0 || mesh_slot >= kMaxMeshSlots ||
        mesh_slots_[mesh_slot].index_count == 0) {
      return Status::Fail("Invalid mesh for instancing");
    }

    UpdateLitInstanceBinding(ib, static_cast<VkDeviceSize>(instance_count * sizeof(Mat4)));

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_);

    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                          static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const MeshSlotGpu& mesh = mesh_slots_[mesh_slot];
    const VkDeviceSize vb_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vb, &vb_offset);
    vkCmdBindIndexBuffer(cmd, mesh.ib, 0, mesh.index_type);

    ObjectGpu od{};
    std::memcpy(od.world, Mat4::Identity().m.data(), sizeof(od.world));
    od.color[0] = prototype.color.r;
    od.color[1] = prototype.color.g;
    od.color[2] = prototype.color.b;
    od.color[3] = prototype.color.a;
    od.metallic = prototype.metallic;
    od.roughness = prototype.roughness;
    od.use_albedo = prototype.use_albedo ? 1.f : 0.f;
    od.use_orm = prototype.use_orm ? 1.f : 0.f;
    od.tex_slot = static_cast<float>(prototype.tex_slot);
    od.uv_scale = prototype.uv_scale > 0.f ? prototype.uv_scale : 1.f;
    od.use_instances = 1.f;
    od.pad = -1.f;

    // Dedicated late slot (matches D3D kMaxLitDraws-1 pattern).
    const std::uint32_t draw_slot = kMaxLitDraws - 1;
    const VkDeviceSize slot =
        (static_cast<VkDeviceSize>(frame_index_) * kMaxLitDraws + draw_slot) * kUniformAlign;
    void* mapped = nullptr;
    if (vkMapMemory(device_, object_ub_mem_, slot, sizeof(od), 0, &mapped) != VK_SUCCESS) {
      return IDevice::DrawLitInstanced(prototype, instance_count);
    }
    std::memcpy(mapped, &od, sizeof(od));
    vkUnmapMemory(device_, object_ub_mem_);

    const std::uint32_t dyn_offsets[2] = {
        static_cast<std::uint32_t>(frame_index_) * static_cast<std::uint32_t>(kFrameUbSize),
        static_cast<std::uint32_t>(slot)};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_layout_, 0, 1,
                            &lit_desc_set_, 2, dyn_offsets);
    vkCmdDrawIndexed(cmd, mesh.index_count, instance_count, 0, 0, 0);
    lit_draws_this_frame_ += instance_count;
    used_graphics_ = true;
    return Status::Ok();
  }

  Status SetupInstanceCullCompute(const std::filesystem::path& cs_spirv) override {
    if (device_ == VK_NULL_HANDLE || cs_spirv.empty()) {
      return Status::Fail("SetupInstanceCullCompute: invalid");
    }
    auto bytes = ReadFileBytes(cs_spirv);
    if (!bytes) {
      return Status::Fail("Cull CS missing: " + cs_spirv.string());
    }
    DestroyCullCompute();

    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 2;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &cull_set_layout_) != VK_SUCCESS) {
      return Status::Fail("Create cull set layout failed");
    }

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = 80;  // float4x4 + 4×uint
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &cull_set_layout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(device_, &pl, nullptr, &cull_pipeline_layout_) != VK_SUCCESS) {
      DestroyCullCompute();
      return Status::Fail("Create cull pipeline layout failed");
    }

    VkShaderModule cs_mod = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(bytes.value(), cs_mod); !st) {
      DestroyCullCompute();
      return st;
    }
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = cs_mod;
    ci.stage.pName = "CSMain";
    ci.layout = cull_pipeline_layout_;
    const VkResult pr =
        vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &cull_pipeline_);
    vkDestroyShaderModule(device_, cs_mod, nullptr);
    if (pr != VK_SUCCESS) {
      DestroyCullCompute();
      return Status::Fail("Create cull compute PSO failed");
    }

    VkDescriptorPoolSize pool_sz{};
    pool_sz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sz.descriptorCount = 2;
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &pool_sz;
    if (vkCreateDescriptorPool(device_, &dpi, nullptr, &cull_desc_pool_) != VK_SUCCESS) {
      DestroyCullCompute();
      return Status::Fail("Create cull desc pool failed");
    }
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = cull_desc_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &cull_set_layout_;
    if (vkAllocateDescriptorSets(device_, &ai, &cull_desc_set_) != VK_SUCCESS) {
      DestroyCullCompute();
      return Status::Fail("Allocate cull desc set failed");
    }

    cull_ready_ = true;
    LogInfo("Vulkan instance cull CS ready (SSBO IndirectArgs + compact indices)");
    return Status::Ok();
  }

  Status DispatchInstanceCull(const Mat4& view_proj, std::uint32_t instance_count,
                              std::uint32_t& out_visible) override {
    out_visible = instance_count;
    if (!cull_ready_ || !frame_recording_ || instance_count == 0) {
      return Status::Ok();
    }
    if (indirect_args_buf_ == VK_NULL_HANDLE) {
      return Status::Fail("DispatchInstanceCull: UploadIndirectIndexedArgs first");
    }

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    if (pass_active_) {
      vkCmdEndRenderPass(cmd);
      pass_active_ = false;
      present_pass_active_ = false;
    }
    if (shadow_pass_active_) {
      vkCmdEndRenderPass(cmd);
      shadow_pass_active_ = false;
    }
    if (local_shadow_pass_active_) {
      vkCmdEndRenderPass(cmd);
      local_shadow_pass_active_ = false;
    }

    // Zero InstanceCount (uint index 1) then CS InterlockedAdd per visible thread.
    if (indirect_zero_upload_ == VK_NULL_HANDLE) {
      const VkMemoryPropertyFlags host =
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if (auto st = CreateBuffer(sizeof(std::uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host,
                                 indirect_zero_upload_, indirect_zero_upload_mem_);
          !st) {
        return st;
      }
      void* mapped = nullptr;
      if (vkMapMemory(device_, indirect_zero_upload_mem_, 0, sizeof(std::uint32_t), 0, &mapped) !=
              VK_SUCCESS ||
          !mapped) {
        return Status::Fail("Map indirect zero upload failed");
      }
      const std::uint32_t z = 0;
      std::memcpy(mapped, &z, sizeof(z));
      vkUnmapMemory(device_, indirect_zero_upload_mem_);
    }

    {
      VkBufferMemoryBarrier to_copy{};
      to_copy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      to_copy.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                              VK_ACCESS_TRANSFER_WRITE_BIT;
      to_copy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_copy.buffer = indirect_args_buf_;
      to_copy.offset = 0;
      to_copy.size = VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &to_copy, 0, nullptr);
    }
    VkBufferCopy zero_region{};
    zero_region.srcOffset = 0;
    zero_region.dstOffset = sizeof(std::uint32_t);
    zero_region.size = sizeof(std::uint32_t);
    vkCmdCopyBuffer(cmd, indirect_zero_upload_, indirect_args_buf_, 1, &zero_region);

    const VkDeviceSize compact_bytes =
        (std::max)(static_cast<VkDeviceSize>(instance_count) * sizeof(std::uint32_t),
                   static_cast<VkDeviceSize>(256));
    if (cull_compact_buf_ == VK_NULL_HANDLE || cull_compact_bytes_ < compact_bytes) {
      vkDeviceWaitIdle(device_);
      if (cull_compact_buf_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, cull_compact_buf_, nullptr);
        cull_compact_buf_ = VK_NULL_HANDLE;
      }
      if (cull_compact_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, cull_compact_mem_, nullptr);
        cull_compact_mem_ = VK_NULL_HANDLE;
      }
      const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      if (auto st = CreateBuffer(compact_bytes, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 cull_compact_buf_, cull_compact_mem_);
          !st) {
        return st;
      }
      cull_compact_bytes_ = compact_bytes;
      UpdateCullDescriptors();
    }

    {
      VkBufferMemoryBarrier to_cs{};
      to_cs.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      to_cs.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      to_cs.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      to_cs.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_cs.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_cs.buffer = indirect_args_buf_;
      to_cs.offset = 0;
      to_cs.size = VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 0, nullptr, 1, &to_cs, 0, nullptr);
    }

    struct CullPC {
      float vp[16];
      std::uint32_t count;
      std::uint32_t pad[3];
    } cb{};
    std::memcpy(cb.vp, view_proj.m.data(), sizeof(cb.vp));
    cb.count = instance_count;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pipeline_layout_, 0, 1,
                            &cull_desc_set_, 0, nullptr);
    vkCmdPushConstants(cmd, cull_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cb), &cb);
    const std::uint32_t groups = (instance_count + 63u) / 64u;
    vkCmdDispatch(cmd, groups, 1, 1);

    {
      VkBufferMemoryBarrier barriers[2]{};
      barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      barriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
      barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barriers[0].buffer = indirect_args_buf_;
      barriers[0].offset = 0;
      barriers[0].size = VK_WHOLE_SIZE;
      barriers[1] = barriers[0];
      barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barriers[1].buffer = cull_compact_buf_;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                           0, 0, nullptr, 2, barriers, 0, nullptr);
    }

    indirect_fallback_instances_ = instance_count;
    engine::SetFeatureOverride("hiz", true);
    engine::SetFeatureOverride("execute_indirect", true);
    engine::SetFeatureOverride("gpu_cull_compact", true);
    return Status::Ok();
  }

  Status UploadIndirectIndexedArgs(std::span<const std::uint32_t> raw_u32) override {
    if (raw_u32.empty() || (raw_u32.size() % 5) != 0 || device_ == VK_NULL_HANDLE) {
      return Status::Fail("Invalid indirect args");
    }
    indirect_args_cpu_.assign(raw_u32.begin(), raw_u32.end());
    if (raw_u32.size() >= 2) {
      indirect_fallback_instances_ = raw_u32[1];
    }
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(raw_u32.size() * sizeof(std::uint32_t));

    if (indirect_args_buf_ == VK_NULL_HANDLE || indirect_args_bytes_ < bytes) {
      vkDeviceWaitIdle(device_);
      DestroyIndirectArgsBuffers(/*keep_uploads=*/true);
      const VkBufferUsageFlags usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      if (auto st = CreateBuffer(bytes, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 indirect_args_buf_, indirect_args_mem_);
          !st) {
        return st;
      }
      indirect_args_bytes_ = bytes;
      if (cull_ready_) {
        UpdateCullDescriptors();
      }
    }

    auto& upload_buf = indirect_args_upload_[frame_index_];
    auto& upload_mem = indirect_args_upload_mem_[frame_index_];
    auto& upload_bytes = indirect_args_upload_bytes_[frame_index_];
    if (upload_buf == VK_NULL_HANDLE || upload_bytes < bytes) {
      if (upload_buf != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, upload_buf, nullptr);
        upload_buf = VK_NULL_HANDLE;
      }
      if (upload_mem != VK_NULL_HANDLE) {
        vkFreeMemory(device_, upload_mem, nullptr);
        upload_mem = VK_NULL_HANDLE;
      }
      const VkMemoryPropertyFlags host =
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if (auto st = CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, upload_buf,
                                 upload_mem);
          !st) {
        return st;
      }
      upload_bytes = bytes;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device_, upload_mem, 0, bytes, 0, &mapped) != VK_SUCCESS || !mapped) {
      return Status::Fail("Map indirect args upload failed");
    }
    std::memcpy(mapped, raw_u32.data(), static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, upload_mem);

    if (frame_recording_) {
      VkCommandBuffer cmd = command_buffers_[frame_index_];
      if (pass_active_) {
        vkCmdEndRenderPass(cmd);
        pass_active_ = false;
        present_pass_active_ = false;
      }
      VkBufferMemoryBarrier to_copy{};
      to_copy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      to_copy.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      to_copy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_copy.buffer = indirect_args_buf_;
      to_copy.offset = 0;
      to_copy.size = VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &to_copy, 0, nullptr);
      VkBufferCopy region{};
      region.size = bytes;
      vkCmdCopyBuffer(cmd, upload_buf, indirect_args_buf_, 1, &region);
      VkBufferMemoryBarrier to_indirect{};
      to_indirect.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      to_indirect.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      to_indirect.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                                  VK_ACCESS_SHADER_WRITE_BIT;
      to_indirect.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_indirect.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_indirect.buffer = indirect_args_buf_;
      to_indirect.offset = 0;
      to_indirect.size = VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 0, nullptr, 1, &to_indirect, 0, nullptr);
    }

    engine::SetFeatureOverride("execute_indirect", true);
    return Status::Ok();
  }

  Status ExecuteIndirectIndexed(std::uint32_t draw_count) override {
    if (draw_count == 0) {
      return Status::Fail("ExecuteIndirect not ready");
    }

    LitDrawItem proto{};
    proto.world = Mat4::Identity();
    proto.color = {0.35f, 0.55f, 0.32f, 1.f};
    proto.metallic = 0.05f;
    proto.roughness = 0.7f;
    proto.use_albedo = false;
    proto.mesh_slot = 0;

    const std::uint32_t fallback_n = [&]() -> std::uint32_t {
      std::uint32_t n = indirect_fallback_instances_;
      if (n == 0 && indirect_args_cpu_.size() >= 2) {
        n = indirect_args_cpu_[1];
      }
      if (n == 0) {
        n = static_cast<std::uint32_t>(instance_worlds_.size());
      }
      return (std::min)(n, static_cast<std::uint32_t>(instance_worlds_.size()));
    }();

    const bool gpu_ready = indirect_args_buf_ != VK_NULL_HANDLE && lit_ready_ &&
                           lit_pipeline_ != VK_NULL_HANDLE && frame_recording_ &&
                           mesh_slots_[0].index_count > 0 &&
                           instance_bufs_[frame_index_] != VK_NULL_HANDLE &&
                           !instance_worlds_.empty();
    if (gpu_ready) {
      if (auto st = ExecuteIndirectIndexedGpu(draw_count, proto); st) {
        return st;
      } else {
        LogWarn(std::string("ExecuteIndirectIndexed GPU path failed, falling back: ") +
                st.message());
      }
    }

    if (fallback_n == 0) {
      return Status::Fail("ExecuteIndirect not ready");
    }
    if (instance_bufs_[frame_index_] != VK_NULL_HANDLE) {
      return DrawLitInstanced(proto, fallback_n);
    }
    std::vector<LitDrawItem> items(fallback_n, proto);
    for (std::uint32_t i = 0; i < fallback_n; ++i) {
      items[i].world = instance_worlds_[i];
    }
    return DrawLitCubes(items);
  }

  Status ExecuteIndirectIndexedGpu(std::uint32_t draw_count, const LitDrawItem& prototype) {
    if (!pass_active_) {
      if (auto st = BeginLitRenderPass(clear_color_); !st) {
        return st;
      }
    }
    UpdateLitInstanceBinding(instance_bufs_[frame_index_],
                             static_cast<VkDeviceSize>(instance_worlds_.size() * sizeof(Mat4)));

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_);

    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                          static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const MeshSlotGpu& mesh = mesh_slots_[0];
    const VkDeviceSize vb_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vb, &vb_offset);
    vkCmdBindIndexBuffer(cmd, mesh.ib, 0, mesh.index_type);

    ObjectGpu od{};
    std::memcpy(od.world, Mat4::Identity().m.data(), sizeof(od.world));
    od.color[0] = prototype.color.r;
    od.color[1] = prototype.color.g;
    od.color[2] = prototype.color.b;
    od.color[3] = prototype.color.a;
    od.metallic = prototype.metallic;
    od.roughness = prototype.roughness;
    od.use_albedo = prototype.use_albedo ? 1.f : 0.f;
    od.use_orm = prototype.use_orm ? 1.f : 0.f;
    od.tex_slot = static_cast<float>(prototype.tex_slot);
    od.uv_scale = prototype.uv_scale > 0.f ? prototype.uv_scale : 1.f;
    od.use_instances = 1.f;
    od.pad = -1.f;

    const std::uint32_t draw_slot = kMaxLitDraws - 1;
    const VkDeviceSize slot =
        (static_cast<VkDeviceSize>(frame_index_) * kMaxLitDraws + draw_slot) * kUniformAlign;
    void* mapped = nullptr;
    if (vkMapMemory(device_, object_ub_mem_, slot, sizeof(od), 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map object CB for ExecuteIndirect failed");
    }
    std::memcpy(mapped, &od, sizeof(od));
    vkUnmapMemory(device_, object_ub_mem_);

    const std::uint32_t dyn_offsets[2] = {
        static_cast<std::uint32_t>(frame_index_) * static_cast<std::uint32_t>(kFrameUbSize),
        static_cast<std::uint32_t>(slot)};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_layout_, 0, 1,
                            &lit_desc_set_, 2, dyn_offsets);

    {
      VkBufferMemoryBarrier to_indirect{};
      to_indirect.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      to_indirect.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      to_indirect.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
      to_indirect.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_indirect.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      to_indirect.buffer = indirect_args_buf_;
      to_indirect.offset = 0;
      to_indirect.size = VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, 1, &to_indirect, 0,
                           nullptr);
    }

    vkCmdDrawIndexedIndirect(cmd, indirect_args_buf_, 0, draw_count,
                             sizeof(VkDrawIndexedIndirectCommand));
    lit_draws_this_frame_ += draw_count;
    used_graphics_ = true;
    engine::SetFeatureOverride("execute_indirect", true);
    return Status::Ok();
  }

  Status SetupPostMesh(const PostShaders& shaders) override {
    DestroyPostResources();
    auto vs = ReadFileBytes(shaders.vs_dxil);
    if (!vs) {
      return vs.status();
    }
    auto ps = ReadFileBytes(shaders.ps_dxil);
    if (!ps) {
      return ps.status();
    }
    if (auto st = EnsurePostUb(); !st) {
      return st;
    }
    if (auto st = CreatePostColorRenderPass(); !st) {
      return st;
    }
    if (auto st = CreatePostPipeline(vs.value(), ps.value()); !st) {
      return st;
    }
    if (lit_ready_) {
      if (auto st = EnsureSceneColor(); !st) {
        return st;
      }
      if (auto st = EnsureHistory(); !st) {
        return st;
      }
      if (auto st = CreatePostFramebuffers(); !st) {
        return st;
      }
    }
    post_stub_ready_ = true;
    LogInfo("Vulkan post mesh ready (SPIR-V SSAO/TAA/fog/bloom/tonemap)");
    return Status::Ok();
  }

  Status ResolvePostEffects(const PostResolveDesc& desc) override {
    if (!post_stub_ready_ || post_pipeline_ == VK_NULL_HANDLE) {
      return Status::Fail("SetupPostMesh not called");
    }
    if (!desc.NeedsResolve()) {
      return Status::Ok();
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    post_exposure_ = desc.exposure > 0.01f ? desc.exposure : 1.f;
    post_tonemap_mode_ = desc.tonemap_mode;

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    if (auto st = CaptureSceneColorIntermediate(cmd); !st) {
      LogWarn(std::string("VK scene_color intermediate: ") + st.message());
      return st;
    }
    if (auto st = EnsureHistory(); !st) {
      return st;
    }
    if (auto st = EnsurePostDescriptors(); !st) {
      return st;
    }
    if (auto st = UploadPostCB(desc); !st) {
      return st;
    }
    UpdatePostDescriptors();

    // Color-only pass: sample depth+history without binding depth as attachment.
    if (pass_active_) {
      vkCmdEndRenderPass(cmd);
      pass_active_ = false;
      present_pass_active_ = false;
      present_pass_load_ = false;
    }
    if (post_framebuffers_.empty() || image_index_ >= post_framebuffers_.size() ||
        post_render_pass_ == VK_NULL_HANDLE) {
      return Status::Fail("Post framebuffers missing");
    }
    if (history_layout_ != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      BarrierHistory(cmd, history_layout_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VkClearValue clear{};
    clear.color = {{clear_color_.r, clear_color_.g, clear_color_.b, clear_color_.a}};
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = post_render_pass_;
    rp.framebuffer = post_framebuffers_[image_index_];
    rp.renderArea.extent = {width_, height_};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_layout_, 0, 1,
                            &post_desc_set_, 0, nullptr);
    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                         static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{};
    scissor.extent = {width_, height_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    used_graphics_ = true;

    // Copy resolved LDR swapchain → history (TAA input next frame).
    if (auto st = CopySwapchainToHistory(cmd); !st) {
      return st;
    }

    // Restore depth write for UI/debug present pass.
    if (depth_layout_ != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      BarrierDepth(cmd, depth_layout_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    if (auto st = BeginPresentRenderPass(clear_color_, /*load_contents=*/true); !st) {
      return st;
    }
    post_resolved_this_frame_ = true;

    if (!post_resolve_warned_) {
      LogInfo("Vulkan ResolvePostEffects full post stack exposure=" +
              std::to_string(post_exposure_) + " tonemap_mode=" +
              std::to_string(post_tonemap_mode_));
      post_resolve_warned_ = true;
    }
    return Status::Ok();
  }

  Status UploadReflectionCubemap(const std::uint8_t* rgba_faces, int face_size) override {
    // Dedicated Fresnel / local probe cube (binding 12); independent of IBL prefilter (binding 8).
    if (!rgba_faces || face_size <= 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid reflection cubemap");
    }
    DestroyReflectionProbeCube();
    if (auto st = UploadCubemapTo(reflection_probe_image_, reflection_probe_mem_,
                                  reflection_probe_view_, rgba_faces, face_size);
        !st) {
      return st;
    }
    if (lit_desc_set_ != VK_NULL_HANDLE && lit_linear_sampler_ != VK_NULL_HANDLE) {
      UpdateLitCombinedBinding(12, reflection_probe_view_, lit_linear_sampler_);
    }
    return Status::Ok();
  }

  Status UploadIblIrradianceCubemap(const std::uint8_t* rgba_faces, int face_size) override {
    return UploadIblCubemapGpu(rgba_faces, face_size, true);
  }

  Status UploadIblPrefilterCubemap(const std::uint8_t* rgba_faces, int face_size) override {
    if (!rgba_faces || face_size <= 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid prefilter cubemap");
    }
    DestroyPrefilterCube();
    if (auto st = UploadCubemapTo(ibl_prefilter_image_, ibl_prefilter_mem_, ibl_prefilter_view_,
                                  rgba_faces, face_size);
        !st) {
      return st;
    }
    if (lit_desc_set_ != VK_NULL_HANDLE && lit_linear_sampler_ != VK_NULL_HANDLE) {
      UpdateLitCombinedBinding(8, ibl_prefilter_view_, lit_linear_sampler_);
    }
    return Status::Ok();
  }

  Status UploadIblBrdfLut(const std::uint8_t* rgba, int w, int h) override {
    if (!rgba || w <= 0 || h <= 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid BRDF LUT size");
    }
    ibl_lut_w_ = w;
    ibl_lut_h_ = h;
    if (auto st = UploadRgba2D(ibl_lut_, rgba, w, h, 9, lit_linear_sampler_); !st) {
      return st;
    }
    if (!ibl_upload_logged_) {
      LogInfo("Vulkan BRDF LUT uploaded (" + std::to_string(w) + "x" + std::to_string(h) + ")");
      ibl_upload_logged_ = true;
    }
    return Status::Ok();
  }

  // Explicit SKIP — do not report bindless capability on Vulkan.
  Status ProbeBindlessMinimalPath(std::uint32_t /*srv_heap_slot*/) override {
    return Status::Fail(
        "ProbeBindlessMinimalPath: Vulkan bindless SKIP (no descriptor-indexing path)");
  }

  Status DrawLitCube(const LitDrawItem& item) override {
    return DrawLitCubes(std::span<const LitDrawItem>(&item, 1));
  }

  Status DrawTransparentLitCubes(std::span<const LitDrawItem> items) override {
    return DrawLitCubesWithPipeline(items, lit_pipeline_transparent_);
  }

  Status DrawLitCubes(std::span<const LitDrawItem> items) override {
    return DrawLitCubesWithPipeline(items, lit_pipeline_);
  }

  Status DrawLitCubesWithPipeline(std::span<const LitDrawItem> items, VkPipeline pipeline) {
    if (!lit_ready_ || pipeline == VK_NULL_HANDLE) {
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                          static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (std::size_t i = 0; i < items.size(); ++i) {
      const int mesh_slot = items[i].mesh_slot;
      if (mesh_slot < 0 || mesh_slot >= kMaxMeshSlots ||
          mesh_slots_[mesh_slot].index_count == 0) {
        continue;
      }
      const MeshSlotGpu& mesh = mesh_slots_[mesh_slot];
      const VkDeviceSize vb_offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vb, &vb_offset);
      vkCmdBindIndexBuffer(cmd, mesh.ib, 0, mesh.index_type);

      ObjectGpu od{};
      std::memcpy(od.world, items[i].world.m.data(), sizeof(od.world));
      od.color[0] = items[i].color.r;
      od.color[1] = items[i].color.g;
      od.color[2] = items[i].color.b;
      od.color[3] = items[i].color.a;
      od.metallic = items[i].metallic;
      od.roughness = items[i].roughness;
      od.use_albedo = items[i].use_albedo ? 1.f : 0.f;
      od.use_orm = items[i].use_orm ? 1.f : 0.f;
      od.tex_slot = static_cast<float>(items[i].tex_slot);
      od.uv_scale = items[i].uv_scale > 0.f ? items[i].uv_scale : 1.f;
      od.use_instances = 0.f;
      od.pad = -1.f;  // classic only; VK has no bindless_hot_path

      const std::uint32_t draw_slot = lit_draws_this_frame_ % kMaxLitDraws;
      const VkDeviceSize slot =
          (static_cast<VkDeviceSize>(frame_index_) * kMaxLitDraws + draw_slot) * kUniformAlign;
      void* mapped = nullptr;
      if (vkMapMemory(device_, object_ub_mem_, slot, sizeof(od), 0, &mapped) != VK_SUCCESS) {
        return Status::Fail("Map object UB failed");
      }
      std::memcpy(mapped, &od, sizeof(od));
      vkUnmapMemory(device_, object_ub_mem_);

      const std::uint32_t dyn_offsets[2] = {
          static_cast<std::uint32_t>(frame_index_) * static_cast<std::uint32_t>(kFrameUbSize),
          static_cast<std::uint32_t>(slot)};
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lit_pipeline_layout_, 0, 1,
                              &lit_desc_set_, 2, dyn_offsets);
      vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
      ++lit_draws_this_frame_;
    }

    used_graphics_ = true;
    return Status::Ok();
  }

  Status UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height,
                             int slot) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (slot < 0 || slot > 1) {
      return Status::Fail("Invalid albedo slot");
    }
    const std::uint32_t binding = (slot == 0) ? 4u : 6u;
    return UploadRgba2D(lit_albedo_[slot], rgba, width, height, binding, lit_linear_sampler_);
  }
  Status UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height, int slot) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (slot < 0 || slot > 1) {
      return Status::Fail("Invalid ORM slot");
    }
    const std::uint32_t binding = (slot == 0) ? 5u : 7u;
    return UploadRgba2D(lit_orm_[slot], rgba, width, height, binding, lit_linear_sampler_);
  }
  Status UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                           std::span<const std::uint32_t> indices) override {
    if (mesh_slot < 0 || mesh_slot >= kMaxMeshSlots) {
      return Status::Fail("Invalid mesh slot");
    }
    if (vertices.empty() || indices.empty()) {
      return Status::Fail("Empty lit geometry");
    }
    if (device_ == VK_NULL_HANDLE) {
      return Status::Fail("Device not ready");
    }

    MeshSlotGpu& slot = mesh_slots_[static_cast<std::size_t>(mesh_slot)];
    if (slot.vb != VK_NULL_HANDLE || slot.ib != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
      DestroyMeshSlot(slot);
    }

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkDeviceSize vb_bytes =
        static_cast<VkDeviceSize>(vertices.size() * sizeof(LitVertex));
    const VkDeviceSize ib_bytes =
        static_cast<VkDeviceSize>(indices.size() * sizeof(std::uint32_t));
    if (auto st = CreateBuffer(vb_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, slot.vb,
                               slot.vb_mem);
        !st) {
      return st;
    }
    if (auto st = CreateBuffer(ib_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host, slot.ib,
                               slot.ib_mem);
        !st) {
      return st;
    }
    void* mapped = nullptr;
    vkMapMemory(device_, slot.vb_mem, 0, vb_bytes, 0, &mapped);
    std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(vb_bytes));
    vkUnmapMemory(device_, slot.vb_mem);
    vkMapMemory(device_, slot.ib_mem, 0, ib_bytes, 0, &mapped);
    std::memcpy(mapped, indices.data(), static_cast<std::size_t>(ib_bytes));
    vkUnmapMemory(device_, slot.ib_mem);
    slot.index_count = static_cast<std::uint32_t>(indices.size());
    slot.index_type = VK_INDEX_TYPE_UINT32;
    return Status::Ok();
  }
  Status DrawScreenQuads(std::span<const ScreenQuad> quads) override {
    if (!quad_ready_) {
      return quads.empty() ? Status::Ok()
                           : Status::Fail("Screen quad PSO not set up (missing quad shader paths)");
    }
    if (quads.empty()) {
      return Status::Ok();
    }
    if (width_ == 0 || height_ == 0) {
      return Status::Fail("Invalid viewport size for screen quads");
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (!pass_active_) {
      if (auto st = BeginPresentRenderPass(clear_color_, post_resolved_this_frame_); !st) {
        return st;
      }
    }

    struct QuadVertex {
      float x, y;
      float r, g, b, a;
    };
    std::vector<QuadVertex> verts;
    verts.reserve(quads.size() * 6);
    const float inv_w = 1.f / static_cast<float>(width_);
    const float inv_h = 1.f / static_cast<float>(height_);
    auto to_ndc = [&](float px, float py, const ColorRgba& c) {
      const float ndc_x = px * inv_w * 2.f - 1.f;
      // D3D-style Y-up NDC; MakeYFlippedViewport flips to upright FB.
      const float ndc_y = 1.f - py * inv_h * 2.f;
      return QuadVertex{ndc_x, ndc_y, c.r, c.g, c.b, c.a};
    };
    for (const auto& q : quads) {
      const auto v00 = to_ndc(q.x0, q.y0, q.color);
      const auto v10 = to_ndc(q.x1, q.y0, q.color);
      const auto v11 = to_ndc(q.x1, q.y1, q.color);
      const auto v01 = to_ndc(q.x0, q.y1, q.color);
      verts.push_back(v00);
      verts.push_back(v10);
      verts.push_back(v11);
      verts.push_back(v00);
      verts.push_back(v11);
      verts.push_back(v01);
    }

    const std::uint32_t bytes =
        static_cast<std::uint32_t>(verts.size() * sizeof(QuadVertex));
    VkBuffer& quad_vb = quad_vb_[frame_index_];
    VkDeviceMemory& quad_vb_mem = quad_vb_mem_[frame_index_];
    if (quad_vb == VK_NULL_HANDLE) {
      return Status::Fail("Screen quad VB not preallocated");
    }
    const std::uint32_t draw_bytes = (std::min)(bytes, quad_vb_capacity_);
    const std::uint32_t draw_verts = draw_bytes / static_cast<std::uint32_t>(sizeof(QuadVertex));
    const std::uint32_t draw_verts_aligned = draw_verts - (draw_verts % 6);
    if (draw_verts_aligned == 0) {
      return Status::Ok();
    }

    void* mapped = nullptr;
    if (vkMapMemory(device_, quad_vb_mem, 0, draw_verts_aligned * sizeof(QuadVertex), 0,
                    &mapped) != VK_SUCCESS) {
      return Status::Fail("Map quad VB failed");
    }
    std::memcpy(mapped, verts.data(), draw_verts_aligned * sizeof(QuadVertex));
    vkUnmapMemory(device_, quad_vb_mem);

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, quad_pipeline_);
    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                          static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &quad_vb, &offset);
    vkCmdDraw(cmd, draw_verts_aligned, 1, 0, 0);
    used_graphics_ = true;
    return Status::Ok();
  }
  Status DrawDebugLines(std::span<const DebugLineVertex> lines_as_segments) override {
    if (!debug_ready_) {
      return lines_as_segments.empty()
                 ? Status::Ok()
                 : Status::Fail("Debug line PSO not set up (missing debug shader paths)");
    }
    if (lines_as_segments.empty()) {
      return Status::Ok();
    }
    if (lines_as_segments.size() % 2 != 0) {
      return Status::Fail("Debug lines require an even vertex count (segments)");
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (!pass_active_) {
      // Prefer LOAD so grid occludes behind scene depth written during OpaqueLit.
      if (auto st = BeginPresentRenderPass(clear_color_, /*load_contents=*/true); !st) {
        return st;
      }
    }

    const std::uint32_t bytes =
        static_cast<std::uint32_t>(lines_as_segments.size() * sizeof(DebugLineVertex));
    VkBuffer& debug_vb = debug_vb_[frame_index_];
    VkDeviceMemory& debug_vb_mem = debug_vb_mem_[frame_index_];
    if (debug_vb == VK_NULL_HANDLE || debug_vb_capacity_ == 0) {
      return Status::Fail("Debug VB not preallocated");
    }
    const std::uint32_t draw_bytes = (std::min)(bytes, debug_vb_capacity_);
    const std::uint32_t draw_verts =
        draw_bytes / static_cast<std::uint32_t>(sizeof(DebugLineVertex));
    const std::uint32_t draw_verts_aligned = draw_verts - (draw_verts % 2);
    if (draw_verts_aligned == 0) {
      return Status::Ok();
    }

    void* mapped = nullptr;
    if (vkMapMemory(device_, debug_vb_mem, 0, draw_verts_aligned * sizeof(DebugLineVertex), 0,
                    &mapped) != VK_SUCCESS) {
      return Status::Fail("Map debug VB failed");
    }
    std::memcpy(mapped, lines_as_segments.data(),
                draw_verts_aligned * sizeof(DebugLineVertex));
    vkUnmapMemory(device_, debug_vb_mem);

    float vp_mat[16]{};
    const Mat4 debug_vp = lighting_.view_proj;
    std::memcpy(vp_mat, debug_vp.m.data(), sizeof(vp_mat));
    const VkDeviceSize cb_off = static_cast<VkDeviceSize>(frame_index_) * kUniformAlign;
    if (vkMapMemory(device_, debug_ub_mem_, cb_off, sizeof(vp_mat), 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map debug UB failed");
    }
    std::memcpy(mapped, vp_mat, sizeof(vp_mat));
    vkUnmapMemory(device_, debug_ub_mem_);

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_pipeline_);
    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                          static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDescriptorBufferInfo buf_info{};
    buf_info.buffer = debug_ub_;
    buf_info.offset = cb_off;
    buf_info.range = sizeof(vp_mat);
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = debug_desc_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buf_info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_pipeline_layout_, 0, 1,
                            &debug_desc_set_, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &debug_vb, &offset);
    vkCmdDraw(cmd, draw_verts_aligned, 1, 0, 0);
    used_graphics_ = true;
    return Status::Ok();
  }
  Status SetupUiMesh(const SimpleMeshShaders& shaders) override {
    vkDeviceWaitIdle(device_);
    DestroyUiResources();

    auto vs = ReadFileBytes(shaders.vs_dxil);
    if (!vs) {
      return vs.status();
    }
    auto ps = ReadFileBytes(shaders.ps_dxil);
    if (!ps) {
      return ps.status();
    }
    if (present_render_pass_ == VK_NULL_HANDLE) {
      return Status::Fail("SetupLitMesh first (UI needs present render pass)");
    }

    VkShaderModule vs_mod = VK_NULL_HANDLE;
    VkShaderModule ps_mod = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(vs.value(), vs_mod); !st) {
      return st;
    }
    if (auto st = CreateShaderModule(ps.value(), ps_mod); !st) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      return st;
    }

    // t/s shift: b0�?, t0/s0�? combined
    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 2;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 2;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &ui_set_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      vkDestroyShaderModule(device_, ps_mod, nullptr);
      return Status::Fail("Create UI set layout failed");
    }
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &ui_set_layout_;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &ui_pipeline_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      vkDestroyShaderModule(device_, ps_mod, nullptr);
      return Status::Fail("Create UI pipeline layout failed");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_mod;
    stages[0].pName = "VSMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_mod;
    stages[1].pName = "PSMain";

    VkVertexInputBindingDescription vbind{};
    vbind.binding = 0;
    vbind.stride = sizeof(UiVertex);
    vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, x)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, u)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiVertex, r)};
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vbind;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.blendEnable = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.colorBlendOp = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.alphaBlendOp = VK_BLEND_OP_ADD;
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
    gp.layout = ui_pipeline_layout_;
    gp.renderPass = present_render_pass_;
    gp.subpass = 0;
    const VkResult r =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &ui_pipeline_);
    vkDestroyShaderModule(device_, vs_mod, nullptr);
    vkDestroyShaderModule(device_, ps_mod, nullptr);
    if (r != VK_SUCCESS) {
      return Status::Fail("Create UI pipeline failed: " + VkErr(r));
    }

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (auto st = CreateBuffer(kUniformAlign * kFramesInFlight, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               host, ui_ub_, ui_ub_mem_);
        !st) {
      return st;
    }
    ui_vb_capacity_ = kMaxUiVerts * static_cast<std::uint32_t>(sizeof(UiVertex));
    ui_ib_capacity_ = kMaxUiIndices * static_cast<std::uint32_t>(sizeof(std::uint16_t));
    for (std::uint32_t fi = 0; fi < kFramesInFlight; ++fi) {
      if (auto st = CreateBuffer(ui_vb_capacity_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host,
                                 ui_vb_[fi], ui_vb_mem_[fi]);
          !st) {
        return st;
      }
      if (auto st = CreateBuffer(ui_ib_capacity_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host,
                                 ui_ib_[fi], ui_ib_mem_[fi]);
          !st) {
        return st;
      }
    }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device_, &si, nullptr, &ui_sampler_) != VK_SUCCESS) {
      return Status::Fail("Create UI sampler failed");
    }

    VkDescriptorPoolSize sizes[2]{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    sizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 1;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device_, &pci, nullptr, &ui_desc_pool_) != VK_SUCCESS) {
      return Status::Fail("Create UI desc pool failed");
    }
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = ui_desc_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &ui_set_layout_;
    if (vkAllocateDescriptorSets(device_, &ai, &ui_desc_set_) != VK_SUCCESS) {
      return Status::Fail("Allocate UI desc set failed");
    }

    ui_ready_ = true;
    return Status::Ok();
  }
  Status UploadUiFontAtlas(const std::uint8_t* rgba, int width, int height) override {
    if (!ui_ready_) {
      return Status::Fail("SetupUiMesh not called");
    }
    if (!rgba || width <= 0 || height <= 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid font atlas size");
    }
    DestroyTex2D(ui_font_);
    if (auto st = CreateAndUploadRgba2D(ui_font_, rgba, width, height); !st) {
      return st;
    }
    VkDescriptorBufferInfo buf{};
    buf.buffer = ui_ub_;
    buf.offset = 0;
    buf.range = 16;
    VkDescriptorImageInfo img{};
    img.sampler = ui_sampler_;
    img.imageView = ui_font_.view;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ui_desc_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buf;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ui_desc_set_;
    writes[1].dstBinding = 2;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &img;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    ui_font_uploaded_ = true;
    return Status::Ok();
  }
  Status DrawUiMesh(std::span<const UiVertex> vertices, std::span<const std::uint16_t> indices,
                    std::span<const UiDrawCmd> commands) override {
    if (commands.empty()) {
      return Status::Ok();
    }
    if (!ui_ready_ || ui_pipeline_ == VK_NULL_HANDLE) {
      return Status::Fail("SetupUiMesh not called");
    }
    if (!ui_font_uploaded_ || ui_font_.view == VK_NULL_HANDLE) {
      return Status::Fail("UploadUiFontAtlas not called");
    }
    if (vertices.empty() || indices.empty()) {
      return Status::Ok();
    }
    if (width_ == 0 || height_ == 0) {
      return Status::Fail("Invalid viewport size for UI mesh");
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (!pass_active_) {
      if (auto st = BeginPresentRenderPass(clear_color_, post_resolved_this_frame_); !st) {
        return st;
      }
    }

    struct UiCBData {
      float inv_display[2];
      float pad[2];
    } cb{};
    cb.inv_display[0] = 1.f / static_cast<float>(width_);
    cb.inv_display[1] = 1.f / static_cast<float>(height_);
    const VkDeviceSize cb_off = static_cast<VkDeviceSize>(frame_index_) * kUniformAlign;
    void* mapped = nullptr;
    if (vkMapMemory(device_, ui_ub_mem_, cb_off, sizeof(cb), 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map UI UB failed");
    }
    std::memcpy(mapped, &cb, sizeof(cb));
    vkUnmapMemory(device_, ui_ub_mem_);

    const std::uint32_t vb_bytes =
        static_cast<std::uint32_t>(vertices.size() * sizeof(UiVertex));
    const std::uint32_t ib_bytes =
        static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t));
    if (ui_vb_capacity_ < vb_bytes || ui_ib_capacity_ < ib_bytes) {
      return Status::Fail("UI mesh exceeds preallocated capacity");
    }
    if (vkMapMemory(device_, ui_vb_mem_[frame_index_], 0, vb_bytes, 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map UI VB failed");
    }
    std::memcpy(mapped, vertices.data(), vb_bytes);
    vkUnmapMemory(device_, ui_vb_mem_[frame_index_]);
    if (vkMapMemory(device_, ui_ib_mem_[frame_index_], 0, ib_bytes, 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map UI IB failed");
    }
    std::memcpy(mapped, indices.data(), ib_bytes);
    vkUnmapMemory(device_, ui_ib_mem_[frame_index_]);

    // Point UB at this frame's slot.
    VkDescriptorBufferInfo buf{};
    buf.buffer = ui_ub_;
    buf.offset = cb_off;
    buf.range = sizeof(cb);
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = ui_desc_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buf;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_layout_, 0, 1,
                            &ui_desc_set_, 0, nullptr);
    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                          static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &ui_vb_[frame_index_], &offset);
    vkCmdBindIndexBuffer(cmd, ui_ib_[frame_index_], 0, VK_INDEX_TYPE_UINT16);

    const float vp_w = static_cast<float>(width_);
    const float vp_h = static_cast<float>(height_);
    for (const auto& c : commands) {
      VkRect2D scissor{};
      scissor.offset.x = static_cast<std::int32_t>((std::max)(0.f, c.clip_x0));
      scissor.offset.y = static_cast<std::int32_t>((std::max)(0.f, c.clip_y0));
      const float r = (std::min)(vp_w, c.clip_x1);
      const float b = (std::min)(vp_h, c.clip_y1);
      scissor.extent.width =
          static_cast<std::uint32_t>((std::max)(0.f, r - static_cast<float>(scissor.offset.x)));
      scissor.extent.height =
          static_cast<std::uint32_t>((std::max)(0.f, b - static_cast<float>(scissor.offset.y)));
      vkCmdSetScissor(cmd, 0, 1, &scissor);
      vkCmdDrawIndexed(cmd, c.index_count, 1, c.index_offset, 0, 0);
    }
    used_graphics_ = true;
    return Status::Ok();
  }

  Status SetupSkybox(const std::filesystem::path& vs_dxil,
                     const std::filesystem::path& ps_dxil) override {
    if (vs_dxil.empty() || ps_dxil.empty()) {
      return Status::Fail("SetupSkybox: invalid");
    }
    if (render_pass_ == VK_NULL_HANDLE) {
      return Status::Fail("SetupLitMesh first");
    }
    vkDeviceWaitIdle(device_);
    DestroySkyResources();

    auto vs = ReadFileBytes(vs_dxil);
    if (!vs) {
      return vs.status();
    }
    auto ps = ReadFileBytes(ps_dxil);
    if (!ps) {
      return ps.status();
    }

    VkShaderModule vs_mod = VK_NULL_HANDLE;
    VkShaderModule ps_mod = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(vs.value(), vs_mod); !st) {
      return st;
    }
    if (auto st = CreateShaderModule(ps.value(), ps_mod); !st) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      return st;
    }

    // With t/s shift: b0→binding0, t0/s0→binding2 combined
    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    binds[1].binding = 2;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 2;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &sky_set_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      vkDestroyShaderModule(device_, ps_mod, nullptr);
      return Status::Fail("Create sky set layout failed");
    }
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &sky_set_layout_;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &sky_pipeline_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      vkDestroyShaderModule(device_, ps_mod, nullptr);
      return Status::Fail("Create sky pipeline layout failed");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_mod;
    stages[0].pName = "VSMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_mod;
    stages[1].pName = "PSMain";

    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
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
    gp.layout = sky_pipeline_layout_;
    gp.renderPass = render_pass_;
    gp.subpass = 0;
    const VkResult r =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &sky_pipeline_);
    vkDestroyShaderModule(device_, vs_mod, nullptr);
    vkDestroyShaderModule(device_, ps_mod, nullptr);
    if (r != VK_SUCCESS) {
      return Status::Fail("Create sky pipeline failed: " + VkErr(r));
    }

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (auto st = CreateBuffer(kUniformAlign * kFramesInFlight, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               host, sky_ub_, sky_ub_mem_);
        !st) {
      return st;
    }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 1.f;
    if (vkCreateSampler(device_, &si, nullptr, &sky_cube_sampler_) != VK_SUCCESS) {
      return Status::Fail("Create sky sampler failed");
    }

    VkDescriptorPoolSize sizes[2]{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    sizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 1;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device_, &pci, nullptr, &sky_desc_pool_) != VK_SUCCESS) {
      return Status::Fail("Create sky desc pool failed");
    }
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = sky_desc_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sky_set_layout_;
    if (vkAllocateDescriptorSets(device_, &ai, &sky_desc_set_) != VK_SUCCESS) {
      return Status::Fail("Allocate sky desc set failed");
    }

    sky_ready_ = true;
    return Status::Ok();
  }

  Status UploadSkyCubemap(const std::uint8_t* rgba_faces, int face_size) override {
    if (!sky_ready_) {
      return Status::Fail("SetupSkybox first");
    }
    if (!rgba_faces || face_size <= 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid sky cubemap");
    }
    DestroySkyCube();
    if (auto st = UploadCubemapTo(sky_cube_image_, sky_cube_mem_, sky_cube_view_, rgba_faces,
                                  face_size);
        !st) {
      return st;
    }
    VkDescriptorBufferInfo buf{};
    buf.buffer = sky_ub_;
    buf.offset = 0;
    buf.range = 64;
    VkDescriptorImageInfo img{};
    img.sampler = sky_cube_sampler_;
    img.imageView = sky_cube_view_;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = sky_desc_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buf;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = sky_desc_set_;
    writes[1].dstBinding = 2;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &img;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    sky_uploaded_ = true;
    engine::SetFeatureOverride("skybox", true);
    return Status::Ok();
  }

  Status DrawSkybox(const Mat4& view_rot_proj) override {
    if (!sky_ready_ || !sky_uploaded_) {
      return Status::Ok();
    }
    if (!frame_recording_) {
      return Status::Fail("BeginFrame not called");
    }
    if (!pass_active_) {
      if (auto st = BeginLitRenderPass(clear_color_); !st) {
        return st;
      }
    }

    const VkDeviceSize cb_off = static_cast<VkDeviceSize>(frame_index_) * kUniformAlign;
    void* mapped = nullptr;
    if (vkMapMemory(device_, sky_ub_mem_, cb_off, sizeof(float) * 16, 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map sky UB failed");
    }
    const Mat4 sky_vp = view_rot_proj;
    std::memcpy(mapped, sky_vp.m.data(), sizeof(float) * 16);
    vkUnmapMemory(device_, sky_ub_mem_);

    VkDescriptorBufferInfo buf{};
    buf.buffer = sky_ub_;
    buf.offset = cb_off;
    buf.range = sizeof(float) * 16;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = sky_desc_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buf;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    VkCommandBuffer cmd = command_buffers_[frame_index_];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline_layout_, 0, 1,
                            &sky_desc_set_, 0, nullptr);
    VkViewport vp = MakeYFlippedViewport(0.f, 0.f, static_cast<float>(width_),
                                          static_cast<float>(height_));
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDraw(cmd, 36, 1, 0, 0);
    used_graphics_ = true;
    return Status::Ok();
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
    if (w <= 0 || h <= 0 || device_ == VK_NULL_HANDLE || swapchain_images_.empty()) {
      return Status::Fail("Vulkan Readback: device not ready");
    }
    if (!frame_recording_) {
      return Status::Fail("Vulkan Readback: BeginFrame not called");
    }
    VkCommandBuffer cmd = command_buffers_[frame_index_];
    if (pass_active_) {
      vkCmdEndRenderPass(cmd);
      pass_active_ = false;
    }
    if (shadow_pass_active_) {
      vkCmdEndRenderPass(cmd);
      shadow_pass_active_ = false;
    }

    const VkDeviceSize row_pitch =
        (static_cast<VkDeviceSize>(w) * 4 + 255ull) & ~255ull;  // align 256
    const VkDeviceSize buf_size = row_pitch * static_cast<VkDeviceSize>(h);
    if (color_readback_buf_ == VK_NULL_HANDLE || color_readback_size_ < buf_size) {
      if (color_readback_buf_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, color_readback_buf_, nullptr);
        color_readback_buf_ = VK_NULL_HANDLE;
      }
      if (color_readback_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, color_readback_mem_, nullptr);
        color_readback_mem_ = VK_NULL_HANDLE;
      }
      if (auto st = CreateBuffer(buf_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 color_readback_buf_, color_readback_mem_);
          !st) {
        return st;
      }
      color_readback_size_ = buf_size;
    }

    VkImage image = swapchain_images_[image_index_];
    // After lit RP, color is typically PRESENT_SRC or COLOR_ATTACHMENT.
    VkImageMemoryBarrier to_src{};
    to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_src.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.image = image;
    to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_src.subresourceRange.levelCount = 1;
    to_src.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_src);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width_, height_, 1};
    region.bufferRowLength = static_cast<std::uint32_t>(row_pitch / 4);
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, color_readback_buf_, 1,
                           &region);

    VkImageMemoryBarrier to_present{};
    to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image = image;
    to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_present.subresourceRange.levelCount = 1;
    to_present.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &to_present);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
      return Status::Fail("Vulkan Readback: EndCommandBuffer failed");
    }
    frame_recording_ = false;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (vkQueueSubmit(graphics_queue_, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
      return Status::Fail("Vulkan Readback: QueueSubmit failed");
    }
    vkQueueWaitIdle(graphics_queue_);

    void* mapped = nullptr;
    if (vkMapMemory(device_, color_readback_mem_, 0, buf_size, 0, &mapped) != VK_SUCCESS ||
        !mapped) {
      return Status::Fail("Vulkan Readback: Map failed");
    }
    const auto* src = static_cast<const std::uint8_t*>(mapped);
    out_rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    const bool bgra = surface_format_.format == VK_FORMAT_B8G8R8A8_UNORM ||
                      surface_format_.format == VK_FORMAT_B8G8R8A8_SRGB;
    const std::size_t row_bytes = static_cast<std::size_t>(w) * 4u;
    if (!bgra) {
      for (int y = 0; y < h; ++y) {
        const auto* row = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(row_pitch);
        std::memcpy(out_rgba.data() + static_cast<std::size_t>(y) * row_bytes, row, row_bytes);
      }
    } else {
      for (int y = 0; y < h; ++y) {
        const auto* row = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(row_pitch);
        std::uint8_t* dst = out_rgba.data() + static_cast<std::size_t>(y) * row_bytes;
        for (int x = 0; x < w; ++x) {
          dst[x * 4 + 0] = row[x * 4 + 2];
          dst[x * 4 + 1] = row[x * 4 + 1];
          dst[x * 4 + 2] = row[x * 4 + 0];
          dst[x * 4 + 3] = row[x * 4 + 3];
        }
      }
    }
    vkUnmapMemory(device_, color_readback_mem_);

    // Resume recording for Present path (Present expects recording ended already �?reopen).
    if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) {
      return Status::Fail("Vulkan Readback: ResetCommandBuffer failed");
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
      return Status::Fail("Vulkan Readback: BeginCommandBuffer failed");
    }
    frame_recording_ = true;
    cleared_ = true;
    used_graphics_ = true;
    return Status::Ok();
  }

 private:
  void DestroyMeshSlot(MeshSlotGpu& slot) {
    if (slot.vb != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, slot.vb, nullptr);
      slot.vb = VK_NULL_HANDLE;
    }
    if (slot.vb_mem != VK_NULL_HANDLE) {
      vkFreeMemory(device_, slot.vb_mem, nullptr);
      slot.vb_mem = VK_NULL_HANDLE;
    }
    if (slot.ib != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, slot.ib, nullptr);
      slot.ib = VK_NULL_HANDLE;
    }
    if (slot.ib_mem != VK_NULL_HANDLE) {
      vkFreeMemory(device_, slot.ib_mem, nullptr);
      slot.ib_mem = VK_NULL_HANDLE;
    }
    slot.index_count = 0;
  }

  void DestroyTex2D(Tex2DGpu& tex) {
    if (tex.view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, tex.view, nullptr);
      tex.view = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
      vkDestroyImage(device_, tex.image, nullptr);
      tex.image = VK_NULL_HANDLE;
    }
    if (tex.mem != VK_NULL_HANDLE) {
      vkFreeMemory(device_, tex.mem, nullptr);
      tex.mem = VK_NULL_HANDLE;
    }
  }

  void UpdateLitCombinedBinding(std::uint32_t binding, VkImageView view, VkSampler sampler) {
    UpdateLitCombinedBinding(binding, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  void UpdateLitInstanceBinding(VkBuffer buffer, VkDeviceSize range) {
    if (lit_desc_set_ == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) {
      return;
    }
    VkDescriptorBufferInfo info{};
    info.buffer = buffer;
    info.offset = 0;
    info.range = range > 0 ? range : VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = lit_desc_set_;
    write.dstBinding = 11;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  void UpdateLitCombinedBinding(std::uint32_t binding, VkImageView view, VkSampler sampler,
                                VkImageLayout layout) {
    if (lit_desc_set_ == VK_NULL_HANDLE || view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
      return;
    }
    VkDescriptorImageInfo info{};
    info.sampler = sampler;
    info.imageView = view;
    info.imageLayout = layout;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = lit_desc_set_;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  Status CreateAndUploadRgba2D(Tex2DGpu& out, const std::uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid RGBA2D");
    }
    const std::uint32_t w = static_cast<std::uint32_t>(width);
    const std::uint32_t h = static_cast<std::uint32_t>(height);
    if (auto st = CreateImage(w, h, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              out.image, out.mem);
        !st) {
      return st;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (auto st = CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               staging, staging_mem);
        !st) {
      return st;
    }
    void* mapped = nullptr;
    vkMapMemory(device_, staging_mem, 0, bytes, 0, &mapped);
    std::memcpy(mapped, rgba, static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, staging_mem);

    VkCommandBuffer cmd = BeginOneShot();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = out.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndOneShot(cmd);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, staging_mem, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = out.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vi, nullptr, &out.view) != VK_SUCCESS) {
      return Status::Fail("Create RGBA2D view failed");
    }
    return Status::Ok();
  }

  Status UploadRgba2D(Tex2DGpu& out, const std::uint8_t* rgba, int width, int height,
                      std::uint32_t binding, VkSampler sampler) {
    if (device_ == VK_NULL_HANDLE) {
      return Status::Fail("Device not ready");
    }
    vkDeviceWaitIdle(device_);
    DestroyTex2D(out);
    if (auto st = CreateAndUploadRgba2D(out, rgba, width, height); !st) {
      return st;
    }
    if (sampler != VK_NULL_HANDLE) {
      UpdateLitCombinedBinding(binding, out.view, sampler);
    }
    return Status::Ok();
  }

  Status UploadCubemapTo(VkImage& image, VkDeviceMemory& mem, VkImageView& view,
                         const std::uint8_t* rgba_faces, int face_size) {
    if (!rgba_faces || face_size <= 0 || device_ == VK_NULL_HANDLE) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid cubemap upload");
    }
    const std::uint32_t size = static_cast<std::uint32_t>(face_size);
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {size, size, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 6;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &image) != VK_SUCCESS) {
      return Status::Fail("vkCreateImage cubemap failed");
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS) {
      return Status::Fail("Allocate cubemap memory failed");
    }
    vkBindImageMemory(device_, image, mem, 0);

    const VkDeviceSize face_bytes = static_cast<VkDeviceSize>(size * size * 4);
    const VkDeviceSize total = face_bytes * 6;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (auto st = CreateBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               staging, staging_mem);
        !st) {
      return st;
    }
    void* mapped = nullptr;
    vkMapMemory(device_, staging_mem, 0, total, 0, &mapped);
    std::memcpy(mapped, rgba_faces, static_cast<std::size_t>(total));
    vkUnmapMemory(device_, staging_mem);

    VkCommandBuffer cmd = BeginOneShot();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 6;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    for (std::uint32_t face = 0; face < 6; ++face) {
      VkBufferImageCopy region{};
      region.bufferOffset = face * face_bytes;
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = 0;
      region.imageSubresource.baseArrayLayer = face;
      region.imageSubresource.layerCount = 1;
      region.imageExtent = {size, size, 1};
      vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                             &region);
    }
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndOneShot(cmd);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, staging_mem, nullptr);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 6;
    if (vkCreateImageView(device_, &vi, nullptr, &view) != VK_SUCCESS) {
      return Status::Fail("vkCreateImageView cubemap failed");
    }
    return Status::Ok();
  }

  Status UploadIblCubemapGpu(const std::uint8_t* rgba_faces, int face_size, bool bind_as_irradiance) {
    if (!rgba_faces || face_size <= 0 || device_ == VK_NULL_HANDLE) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid cubemap upload");
    }
    DestroyIblCube();
    if (auto st = UploadCubemapTo(ibl_cube_image_, ibl_cube_mem_, ibl_cube_view_, rgba_faces,
                                  face_size);
        !st) {
      return st;
    }
    if (ibl_sampler_ == VK_NULL_HANDLE) {
      VkSamplerCreateInfo si{};
      si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      si.magFilter = VK_FILTER_LINEAR;
      si.minFilter = VK_FILTER_LINEAR;
      si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.maxLod = 1.f;
      if (vkCreateSampler(device_, &si, nullptr, &ibl_sampler_) != VK_SUCCESS) {
        return Status::Fail("vkCreateSampler IBL failed");
      }
    }
    if (lit_desc_set_ != VK_NULL_HANDLE && bind_as_irradiance) {
      UpdateLitCombinedBinding(3, ibl_cube_view_, ibl_sampler_);
    }
    if (!ibl_upload_logged_) {
      LogInfo("Vulkan IBL cubemap uploaded (" + std::to_string(face_size) + "^2 x6)");
      ibl_upload_logged_ = true;
    }
    return Status::Ok();
  }

  void DestroyIblCube() {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (ibl_cube_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, ibl_cube_view_, nullptr);
      ibl_cube_view_ = VK_NULL_HANDLE;
    }
    if (ibl_cube_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, ibl_cube_image_, nullptr);
      ibl_cube_image_ = VK_NULL_HANDLE;
    }
    if (ibl_cube_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, ibl_cube_mem_, nullptr);
      ibl_cube_mem_ = VK_NULL_HANDLE;
    }
  }

  void DestroyPrefilterCube() {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (ibl_prefilter_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, ibl_prefilter_view_, nullptr);
      ibl_prefilter_view_ = VK_NULL_HANDLE;
    }
    if (ibl_prefilter_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, ibl_prefilter_image_, nullptr);
      ibl_prefilter_image_ = VK_NULL_HANDLE;
    }
    if (ibl_prefilter_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, ibl_prefilter_mem_, nullptr);
      ibl_prefilter_mem_ = VK_NULL_HANDLE;
    }
  }

  void DestroyReflectionProbeCube() {
    if (device_ == VK_NULL_HANDLE) {
      return;
    }
    if (reflection_probe_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, reflection_probe_view_, nullptr);
      reflection_probe_view_ = VK_NULL_HANDLE;
    }
    if (reflection_probe_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, reflection_probe_image_, nullptr);
      reflection_probe_image_ = VK_NULL_HANDLE;
    }
    if (reflection_probe_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, reflection_probe_mem_, nullptr);
      reflection_probe_mem_ = VK_NULL_HANDLE;
    }
  }

  void DestroySkyCube() {
    if (sky_cube_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, sky_cube_view_, nullptr);
      sky_cube_view_ = VK_NULL_HANDLE;
    }
    if (sky_cube_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, sky_cube_image_, nullptr);
      sky_cube_image_ = VK_NULL_HANDLE;
    }
    if (sky_cube_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, sky_cube_mem_, nullptr);
      sky_cube_mem_ = VK_NULL_HANDLE;
    }
    sky_uploaded_ = false;
  }

  void DestroySkyResources() {
    sky_ready_ = false;
    sky_uploaded_ = false;
    DestroySkyCube();
    if (sky_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, sky_pipeline_, nullptr);
      sky_pipeline_ = VK_NULL_HANDLE;
    }
    if (sky_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, sky_pipeline_layout_, nullptr);
      sky_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (sky_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, sky_set_layout_, nullptr);
      sky_set_layout_ = VK_NULL_HANDLE;
    }
    sky_desc_set_ = VK_NULL_HANDLE;
    if (sky_desc_pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, sky_desc_pool_, nullptr);
      sky_desc_pool_ = VK_NULL_HANDLE;
    }
    if (sky_cube_sampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, sky_cube_sampler_, nullptr);
      sky_cube_sampler_ = VK_NULL_HANDLE;
    }
    if (sky_ub_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, sky_ub_, nullptr);
      sky_ub_ = VK_NULL_HANDLE;
    }
    if (sky_ub_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, sky_ub_mem_, nullptr);
      sky_ub_mem_ = VK_NULL_HANDLE;
    }
  }

  void DestroyUiResources() {
    ui_ready_ = false;
    ui_font_uploaded_ = false;
    DestroyTex2D(ui_font_);
    if (ui_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, ui_pipeline_, nullptr);
      ui_pipeline_ = VK_NULL_HANDLE;
    }
    if (ui_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, ui_pipeline_layout_, nullptr);
      ui_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (ui_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, ui_set_layout_, nullptr);
      ui_set_layout_ = VK_NULL_HANDLE;
    }
    ui_desc_set_ = VK_NULL_HANDLE;
    if (ui_desc_pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, ui_desc_pool_, nullptr);
      ui_desc_pool_ = VK_NULL_HANDLE;
    }
    if (ui_sampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, ui_sampler_, nullptr);
      ui_sampler_ = VK_NULL_HANDLE;
    }
    if (ui_ub_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, ui_ub_, nullptr);
      ui_ub_ = VK_NULL_HANDLE;
    }
    if (ui_ub_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, ui_ub_mem_, nullptr);
      ui_ub_mem_ = VK_NULL_HANDLE;
    }
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      if (ui_vb_[i] != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, ui_vb_[i], nullptr);
        ui_vb_[i] = VK_NULL_HANDLE;
      }
      if (ui_vb_mem_[i] != VK_NULL_HANDLE) {
        vkFreeMemory(device_, ui_vb_mem_[i], nullptr);
        ui_vb_mem_[i] = VK_NULL_HANDLE;
      }
      if (ui_ib_[i] != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, ui_ib_[i], nullptr);
        ui_ib_[i] = VK_NULL_HANDLE;
      }
      if (ui_ib_mem_[i] != VK_NULL_HANDLE) {
        vkFreeMemory(device_, ui_ib_mem_[i], nullptr);
        ui_ib_mem_[i] = VK_NULL_HANDLE;
      }
    }
    ui_vb_capacity_ = 0;
    ui_ib_capacity_ = 0;
  }

  void DestroyQuadResources() {
    quad_ready_ = false;
    if (quad_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, quad_pipeline_, nullptr);
      quad_pipeline_ = VK_NULL_HANDLE;
    }
    if (quad_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, quad_pipeline_layout_, nullptr);
      quad_pipeline_layout_ = VK_NULL_HANDLE;
    }
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      if (quad_vb_[i] != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, quad_vb_[i], nullptr);
        quad_vb_[i] = VK_NULL_HANDLE;
      }
      if (quad_vb_mem_[i] != VK_NULL_HANDLE) {
        vkFreeMemory(device_, quad_vb_mem_[i], nullptr);
        quad_vb_mem_[i] = VK_NULL_HANDLE;
      }
    }
    quad_vb_capacity_ = 0;
  }

  void DestroyDebugResources() {
    debug_ready_ = false;
    if (debug_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, debug_pipeline_, nullptr);
      debug_pipeline_ = VK_NULL_HANDLE;
    }
    if (debug_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, debug_pipeline_layout_, nullptr);
      debug_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (debug_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, debug_set_layout_, nullptr);
      debug_set_layout_ = VK_NULL_HANDLE;
    }
    debug_desc_set_ = VK_NULL_HANDLE;
    if (debug_desc_pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, debug_desc_pool_, nullptr);
      debug_desc_pool_ = VK_NULL_HANDLE;
    }
    if (debug_ub_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, debug_ub_, nullptr);
      debug_ub_ = VK_NULL_HANDLE;
    }
    if (debug_ub_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, debug_ub_mem_, nullptr);
      debug_ub_mem_ = VK_NULL_HANDLE;
    }
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      if (debug_vb_[i] != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, debug_vb_[i], nullptr);
        debug_vb_[i] = VK_NULL_HANDLE;
      }
      if (debug_vb_mem_[i] != VK_NULL_HANDLE) {
        vkFreeMemory(device_, debug_vb_mem_[i], nullptr);
        debug_vb_mem_[i] = VK_NULL_HANDLE;
      }
    }
    debug_vb_capacity_ = 0;
  }

  Status SetupScreenQuads(const std::filesystem::path& vs_path,
                          const std::filesystem::path& ps_path) {
    DestroyQuadResources();
    auto vs = ReadFileBytes(vs_path);
    if (!vs) {
      return vs.status();
    }
    auto ps = ReadFileBytes(ps_path);
    if (!ps) {
      return ps.status();
    }
    VkShaderModule vs_mod = VK_NULL_HANDLE;
    VkShaderModule ps_mod = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(vs.value(), vs_mod); !st) {
      return st;
    }
    if (auto st = CreateShaderModule(ps.value(), ps_mod); !st) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      return st;
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &quad_pipeline_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      vkDestroyShaderModule(device_, ps_mod, nullptr);
      return Status::Fail("Create quad pipeline layout failed");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_mod;
    stages[0].pName = "VSMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_mod;
    stages[1].pName = "PSMain";

    VkVertexInputBindingDescription vbind{};
    vbind.binding = 0;
    vbind.stride = 6 * sizeof(float);
    vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8};
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vbind;
    vi.vertexAttributeDescriptionCount = 2;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.blendEnable = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.colorBlendOp = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.alphaBlendOp = VK_BLEND_OP_ADD;
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
    gp.layout = quad_pipeline_layout_;
    gp.renderPass = present_render_pass_;
    gp.subpass = 0;
    const VkResult r =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &quad_pipeline_);
    vkDestroyShaderModule(device_, vs_mod, nullptr);
    vkDestroyShaderModule(device_, ps_mod, nullptr);
    if (r != VK_SUCCESS) {
      return Status::Fail("Create quad pipeline failed: " + VkErr(r));
    }

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    quad_vb_capacity_ = kMaxScreenQuads * 6 * 6 * sizeof(float);
    for (std::uint32_t fi = 0; fi < kFramesInFlight; ++fi) {
      if (auto st = CreateBuffer(quad_vb_capacity_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host,
                                 quad_vb_[fi], quad_vb_mem_[fi]);
          !st) {
        return st;
      }
    }
    quad_ready_ = true;
    return Status::Ok();
  }

  Status SetupDebugLines(const std::filesystem::path& vs_path,
                         const std::filesystem::path& ps_path) {
    DestroyDebugResources();
    auto vs = ReadFileBytes(vs_path);
    if (!vs) {
      return vs.status();
    }
    auto ps = ReadFileBytes(ps_path);
    if (!ps) {
      return ps.status();
    }
    VkShaderModule vs_mod = VK_NULL_HANDLE;
    VkShaderModule ps_mod = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(vs.value(), vs_mod); !st) {
      return st;
    }
    if (auto st = CreateShaderModule(ps.value(), ps_mod); !st) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      return st;
    }

    VkDescriptorSetLayoutBinding bind{};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 1;
    dsl.pBindings = &bind;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &debug_set_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      vkDestroyShaderModule(device_, ps_mod, nullptr);
      return Status::Fail("Create debug set layout failed");
    }
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &debug_set_layout_;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &debug_pipeline_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs_mod, nullptr);
      vkDestroyShaderModule(device_, ps_mod, nullptr);
      return Status::Fail("Create debug pipeline layout failed");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs_mod;
    stages[0].pName = "VSMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps_mod;
    stages[1].pName = "PSMain";

    VkVertexInputBindingDescription vbind{};
    vbind.binding = 0;
    vbind.stride = sizeof(DebugLineVertex);
    vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DebugLineVertex, x)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DebugLineVertex, r)};
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vbind;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
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
    gp.layout = debug_pipeline_layout_;
    gp.renderPass = present_render_pass_;
    gp.subpass = 0;
    const VkResult r =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &debug_pipeline_);
    vkDestroyShaderModule(device_, vs_mod, nullptr);
    vkDestroyShaderModule(device_, ps_mod, nullptr);
    if (r != VK_SUCCESS) {
      return Status::Fail("Create debug pipeline failed: " + VkErr(r));
    }

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (auto st = CreateBuffer(kUniformAlign * kFramesInFlight, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               host, debug_ub_, debug_ub_mem_);
        !st) {
      return st;
    }
    debug_vb_capacity_ = kMaxDebugVerts * static_cast<std::uint32_t>(sizeof(DebugLineVertex));
    for (std::uint32_t fi = 0; fi < kFramesInFlight; ++fi) {
      if (auto st = CreateBuffer(debug_vb_capacity_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host,
                                 debug_vb_[fi], debug_vb_mem_[fi]);
          !st) {
        return st;
      }
    }

    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &size;
    if (vkCreateDescriptorPool(device_, &pci, nullptr, &debug_desc_pool_) != VK_SUCCESS) {
      return Status::Fail("Create debug desc pool failed");
    }
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = debug_desc_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &debug_set_layout_;
    if (vkAllocateDescriptorSets(device_, &ai, &debug_desc_set_) != VK_SUCCESS) {
      return Status::Fail("Allocate debug desc set failed");
    }

    debug_ready_ = true;
    return Status::Ok();
  }

  Status AcceptIblUploadOnce() {
    if (!ibl_upload_logged_) {
      LogInfo("Vulkan IBL upload accepted (sampling parity TBD)");
      ibl_upload_logged_ = true;
    }
    return Status::Ok();
  }

  // load_contents: resume after CaptureSceneColor / mid-frame end without wiping depth.
  // Required so debug grid depth-tests against lit geometry (matches D3D12 DSV reuse).
  Status BeginLitRenderPass(const ColorRgba& color, bool load_contents = false) {
    if (pass_active_) {
      cleared_ = true;
      used_graphics_ = true;
      return Status::Ok();
    }
    if (hdr_framebuffer_ == VK_NULL_HANDLE) {
      return Status::Fail("HDR framebuffer missing");
    }
    if (load_contents && render_pass_load_ == VK_NULL_HANDLE) {
      return Status::Fail("Lit load render pass missing");
    }

    VkClearValue clears[2]{};
    clears[0].color = {{color.r, color.g, color.b, color.a}};
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = load_contents ? render_pass_load_ : render_pass_;
    rp.framebuffer = hdr_framebuffer_;
    rp.renderArea.extent = {width_, height_};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    vkCmdBeginRenderPass(command_buffers_[frame_index_], &rp, VK_SUBPASS_CONTENTS_INLINE);
    pass_active_ = true;
    present_pass_active_ = false;
    present_pass_load_ = false;
    depth_layout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    cleared_ = true;
    used_graphics_ = true;
    return Status::Ok();
  }

  Status BeginPresentRenderPass(const ColorRgba& color, bool load_contents = false) {
    if (pass_active_) {
      cleared_ = true;
      used_graphics_ = true;
      return Status::Ok();
    }
    if (framebuffers_.empty() || image_index_ >= framebuffers_.size()) {
      return Status::Fail("Present framebuffers missing");
    }
    if (load_contents && present_render_pass_load_ == VK_NULL_HANDLE) {
      return Status::Fail("Present load render pass missing");
    }
    if (!load_contents && present_render_pass_ == VK_NULL_HANDLE) {
      return Status::Fail("Present render pass missing");
    }

    VkClearValue clears[2]{};
    clears[0].color = {{color.r, color.g, color.b, color.a}};
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = load_contents ? present_render_pass_load_ : present_render_pass_;
    rp.framebuffer = framebuffers_[image_index_];
    rp.renderArea.extent = {width_, height_};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    vkCmdBeginRenderPass(command_buffers_[frame_index_], &rp, VK_SUBPASS_CONTENTS_INLINE);
    pass_active_ = true;
    present_pass_active_ = true;
    present_pass_load_ = load_contents;
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
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = exts;
    if (enable_validation_) {
      ci.enabledLayerCount = 1;
      ci.ppEnabledLayerNames = layers;
    }

    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS && enable_validation_) {
      LogWarn("Vulkan validation layers unavailable �?retry without");
      ci.enabledLayerCount = 0;
      ci.ppEnabledLayerNames = nullptr;
      r = vkCreateInstance(&ci, nullptr, &instance_);
    }
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

    struct Candidate {
      VkPhysicalDevice pd = VK_NULL_HANDLE;
      std::uint32_t graphics = 0;
      std::uint32_t present = 0;
      int index = 0;
      bool discrete = false;
      std::uint64_t vram = 0;
      std::string name;
    };
    std::vector<Candidate> candidates;

    for (std::uint32_t di = 0; di < count; ++di) {
      VkPhysicalDevice pd = devices[di];
      std::uint32_t qcount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
      std::vector<VkQueueFamilyProperties> qprops(qcount);
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());

      std::int32_t graphics = -1;
      std::int32_t present = -1;
      for (std::uint32_t i = 0; i < qcount; ++i) {
        if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphics < 0) {
          graphics = static_cast<std::int32_t>(i);
        }
        VkBool32 support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &support);
        if (support && present < 0) {
          present = static_cast<std::int32_t>(i);
        }
        if (graphics >= 0 && present >= 0 && graphics == present) {
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

      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(pd, &props);
      VkPhysicalDeviceMemoryProperties mem{};
      vkGetPhysicalDeviceMemoryProperties(pd, &mem);
      std::uint64_t vram = 0;
      for (std::uint32_t mi = 0; mi < mem.memoryHeapCount; ++mi) {
        if (mem.memoryHeaps[mi].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
          vram += mem.memoryHeaps[mi].size;
        }
      }

      Candidate c;
      c.pd = pd;
      c.graphics = static_cast<std::uint32_t>(graphics);
      c.present = static_cast<std::uint32_t>(present);
      c.index = static_cast<int>(di);
      c.discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
      c.vram = vram;
      c.name = props.deviceName;
      candidates.push_back(std::move(c));
    }

    if (candidates.empty()) {
      return Status::Fail("No suitable Vulkan GPU with swapchain + present");
    }

    const Candidate* chosen = nullptr;
    if (adapter_index_ >= 0) {
      for (const auto& c : candidates) {
        if (c.index == adapter_index_) {
          chosen = &c;
          break;
        }
      }
      if (!chosen) {
        return Status::Fail("Vulkan adapter_index=" + std::to_string(adapter_index_) +
                            " not suitable (use --list-gpus)");
      }
    } else {
      // Match D3D HIGH_PERFORMANCE preference: discrete first, then most VRAM.
      chosen = &candidates[0];
      for (const auto& c : candidates) {
        if (c.discrete && !chosen->discrete) {
          chosen = &c;
        } else if (c.discrete == chosen->discrete && c.vram > chosen->vram) {
          chosen = &c;
        }
      }
    }

    physical_ = chosen->pd;
    graphics_family_ = chosen->graphics;
    present_family_ = chosen->present;
    vkGetPhysicalDeviceMemoryProperties(physical_, &mem_props_);
    LogInfo(std::string("Vulkan adapter[") + std::to_string(chosen->index) +
            (chosen->discrete ? " discrete]: " : "]: ") + chosen->name + " vram≈" +
            std::to_string(chosen->vram / (1024ull * 1024ull)) + "MB");
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
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceFeatures available{};
    vkGetPhysicalDeviceFeatures(physical_, &available);
    if (available.depthClamp) {
      features.depthClamp = VK_TRUE;
      depth_clamp_enabled_ = true;
    } else {
      depth_clamp_enabled_ = false;
      LogWarn("Vulkan depthClamp unavailable; transparent lit uses clip (may differ from D3D)");
    }

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<std::uint32_t>(qcis.size());
    ci.pQueueCreateInfos = qcis.data();
    ci.pEnabledFeatures = &features;
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
    if (vsync_) {
      present_mode = VK_PRESENT_MODE_FIFO_KHR;
    } else {
      present_mode = VK_PRESENT_MODE_FIFO_KHR;
      for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
          present_mode = m;
          break;
        }
      }
      if (present_mode == VK_PRESENT_MODE_FIFO_KHR) {
        for (auto m : modes) {
          if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            present_mode = m;
            break;
          }
        }
      }
    }
    LogInfo(std::string("Vulkan presentMode=") +
            (present_mode == VK_PRESENT_MODE_MAILBOX_KHR
                 ? "MAILBOX"
                 : present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE" : "FIFO") +
            (vsync_ ? " (vsync)" : " (uncapped)"));

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
    DestroyPostFramebuffersOnly();
    DestroyDepthOnly();
    DestroySwapchainViews();
  }

  Status RecreateSwapchain() {
    if (device_ == VK_NULL_HANDLE) {
      return Status::Fail("No device");
    }
    vkDeviceWaitIdle(device_);
    DestroyFramebuffersOnly();
    DestroyPostFramebuffersOnly();
    DestroySceneColorOnly();
    DestroyHistoryOnly();
    DestroyDepthOnly();
    DestroySwapchainViews();
    if (lit_ready_) {
      DestroyPresentRenderPasses();
    }
    if (auto st = CreateSwapchain(); !st) {
      return st;
    }
    if (lit_ready_) {
      if (auto st = CreateDepthResources(); !st) {
        return st;
      }
      if (auto st = CreatePresentRenderPass(); !st) {
        return st;
      }
      if (auto st = EnsureSceneColor(); !st) {
        return st;
      }
      if (post_stub_ready_) {
        if (auto st = EnsureHistory(); !st) {
          return st;
        }
      }
      if (auto st = CreateFramebuffers(); !st) {
        return st;
      }
      if (post_stub_ready_) {
        if (auto st = CreatePostFramebuffers(); !st) {
          return st;
        }
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

  VkDeviceSize ShadowVpUbOffset(int slot) const {
    const int s = (std::max)(0, (std::min)(slot, static_cast<int>(kShadowVpSlots) - 1));
    return (static_cast<VkDeviceSize>(frame_index_) * kShadowVpSlots +
            static_cast<VkDeviceSize>(s)) *
           kUniformAlign;
  }

  VkCommandBuffer BeginOneShot() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &ai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
  }

  void EndOneShot(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
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

  void BarrierLocalShadowImage(VkCommandBuffer cmd, VkImageLayout old_layout,
                               VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = local_shadow_image_;
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
    } else {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = 0;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    local_shadow_layout_ = new_layout;
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
    if (auto st = CreateLitRenderPass(); !st) {
      return st;
    }
    return CreatePresentRenderPass();
  }

  Status CreateLitRenderPass() {
    if (render_pass_ != VK_NULL_HANDLE) {
      return Status::Ok();
    }
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

    // Lit/sky: HDR offscreen + depth. Keep HDR in COLOR_ATTACHMENT for post sampling.
    VkAttachmentDescription color{};
    color.format = kHdrColorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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

    VkAttachmentDescription color_load = color;
    color_load.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_load.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription depth_load = depth;
    depth_load.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_load.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentDescription load_atts[] = {color_load, depth_load};
    rpci.pAttachments = load_atts;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &render_pass_load_) != VK_SUCCESS) {
      return Status::Fail("vkCreateRenderPass (load) failed");
    }
    return Status::Ok();
  }

  void DestroyPresentRenderPasses() {
    if (present_render_pass_load_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, present_render_pass_load_, nullptr);
      present_render_pass_load_ = VK_NULL_HANDLE;
    }
    if (present_render_pass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, present_render_pass_, nullptr);
      present_render_pass_ = VK_NULL_HANDLE;
    }
  }

  Status CreatePresentRenderPass() {
    DestroyPresentRenderPasses();

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

    // Post/debug/UI: LDR swapchain. Clear color, load depth from lit pass.
    VkAttachmentDescription color{};
    color.format = surface_format_.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentDescription atts[] = {color, depth};
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &present_render_pass_) != VK_SUCCESS) {
      return Status::Fail("vkCreateRenderPass (present) failed");
    }

    VkAttachmentDescription color_load = color;
    color_load.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_load.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_load.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    const VkAttachmentDescription load_atts[] = {color_load, depth};
    rpci.pAttachments = load_atts;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &present_render_pass_load_) != VK_SUCCESS) {
      return Status::Fail("vkCreateRenderPass (present load) failed");
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
    depth_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
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
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    depth_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    return Status::Ok();
  }

  void DestroyFramebuffersOnly() {
    if (hdr_framebuffer_ != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, hdr_framebuffer_, nullptr);
      hdr_framebuffer_ = VK_NULL_HANDLE;
    }
    for (VkFramebuffer fb : framebuffers_) {
      if (fb != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, fb, nullptr);
      }
    }
    framebuffers_.clear();
  }

  Status RecreateHdrFramebuffer() {
    if (hdr_framebuffer_ != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, hdr_framebuffer_, nullptr);
      hdr_framebuffer_ = VK_NULL_HANDLE;
    }
    if (render_pass_ == VK_NULL_HANDLE || scene_color_view_ == VK_NULL_HANDLE ||
        depth_view_ == VK_NULL_HANDLE) {
      return Status::Ok();
    }
    const VkImageView hdr_atts[] = {scene_color_view_, depth_view_};
    VkFramebufferCreateInfo hfbi{};
    hfbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    hfbi.renderPass = render_pass_;
    hfbi.attachmentCount = 2;
    hfbi.pAttachments = hdr_atts;
    hfbi.width = width_;
    hfbi.height = height_;
    hfbi.layers = 1;
    if (vkCreateFramebuffer(device_, &hfbi, nullptr, &hdr_framebuffer_) != VK_SUCCESS) {
      return Status::Fail("vkCreateFramebuffer (HDR) failed");
    }
    return Status::Ok();
  }

  Status CreateFramebuffers() {
    DestroyFramebuffersOnly();
    if (auto st = EnsureSceneColor(); !st) {
      return st;
    }
    if (present_render_pass_ == VK_NULL_HANDLE) {
      return Status::Fail("Present render pass missing for framebuffers");
    }
    if (auto st = RecreateHdrFramebuffer(); !st) {
      return st;
    }

    framebuffers_.resize(swapchain_views_.size());
    for (std::size_t i = 0; i < swapchain_views_.size(); ++i) {
      const VkImageView atts[] = {swapchain_views_[i], depth_view_};
      VkFramebufferCreateInfo fi{};
      fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fi.renderPass = present_render_pass_;
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

    // Lit set: b0/b1 UBO, combined t0..t8 → bindings 2..10, SSBO t9→11, probe t10→12.
    VkDescriptorSetLayoutBinding binds[13]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (std::uint32_t i = 0; i < 9; ++i) {
      binds[2 + i].binding = 2 + i;
      binds[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      binds[2 + i].descriptorCount = 1;
      binds[2 + i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    // g_instances SSBO: HLSL t9 + -fvk-t-shift 2 → binding 11.
    binds[11].binding = 11;
    binds[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[11].descriptorCount = 1;
    binds[11].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    // g_reflection_probe: HLSL t10 + -fvk-t-shift 2 → binding 12.
    binds[12].binding = 12;
    binds[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[12].descriptorCount = 1;
    binds[12].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 13;
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
    // NONE: neg-height VP + mesh winding can disagree; BACK cull previously hid the
    // ground plane and made scale pillars look toppled (only back faces remained).
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;  // neg-height VP + CCW meshes
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
    if (r != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs, nullptr);
      vkDestroyShaderModule(device_, ps, nullptr);
      return Status::Fail("vkCreateGraphicsPipelines failed: " + VkErr(r));
    }

    // Transparent: SrcAlpha blend, no depth write, depth clamp (D3D DepthClipEnable=FALSE).
    rs.depthClampEnable = depth_clamp_enabled_ ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    blend_att.blendEnable = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.colorBlendOp = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.alphaBlendOp = VK_BLEND_OP_ADD;
    const VkResult rt = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                                  &lit_pipeline_transparent_);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, ps, nullptr);
    if (rt != VK_SUCCESS) {
      return Status::Fail("vkCreateGraphicsPipelines (transparent) failed: " + VkErr(rt));
    }
    return Status::Ok();
  }

  Status CreatePostColorRenderPass() {
    if (post_render_pass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, post_render_pass_, nullptr);
      post_render_pass_ = VK_NULL_HANDLE;
    }
    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription color{};
    color.format = surface_format_.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &post_render_pass_) != VK_SUCCESS) {
      return Status::Fail("Create post color render pass failed");
    }
    return Status::Ok();
  }

  void DestroyPostFramebuffersOnly() {
    for (VkFramebuffer fb : post_framebuffers_) {
      if (fb != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, fb, nullptr);
      }
    }
    post_framebuffers_.clear();
  }

  Status CreatePostFramebuffers() {
    DestroyPostFramebuffersOnly();
    if (post_render_pass_ == VK_NULL_HANDLE || swapchain_views_.empty()) {
      return Status::Ok();
    }
    post_framebuffers_.resize(swapchain_views_.size());
    for (std::size_t i = 0; i < swapchain_views_.size(); ++i) {
      VkFramebufferCreateInfo fi{};
      fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fi.renderPass = post_render_pass_;
      fi.attachmentCount = 1;
      fi.pAttachments = &swapchain_views_[i];
      fi.width = width_;
      fi.height = height_;
      fi.layers = 1;
      if (vkCreateFramebuffer(device_, &fi, nullptr, &post_framebuffers_[i]) != VK_SUCCESS) {
        return Status::Fail("Create post framebuffer failed");
      }
    }
    return Status::Ok();
  }

  Status EnsurePostUb() {
    if (post_ub_ != VK_NULL_HANDLE) {
      return Status::Ok();
    }
    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    return CreateBuffer(kPostUbSize * kFramesInFlight, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host,
                        post_ub_, post_ub_mem_);
  }

  Status UploadPostCB(const PostResolveDesc& desc) {
    if (post_ub_ == VK_NULL_HANDLE || post_ub_mem_ == VK_NULL_HANDLE) {
      return Status::Fail("Post UB missing");
    }
    PostCB cb{};
    cb.inv_res[0] = 1.f / static_cast<float>((std::max)(1u, width_));
    cb.inv_res[1] = 1.f / static_cast<float>((std::max)(1u, height_));
    cb.enable_ssao = desc.enable_ssao ? 1.f : 0.f;
    cb.enable_taa = desc.enable_taa ? 1.f : 0.f;
    cb.ssao_radius = desc.ssao_radius;
    cb.ssao_intensity = desc.ssao_intensity;
    cb.taa_blend = desc.taa_blend;
    cb.exposure = desc.exposure;
    std::memcpy(cb.inv_view_proj, desc.inv_view_proj.m.data(), sizeof(cb.inv_view_proj));
    std::memcpy(cb.view_proj, desc.view_proj.m.data(), sizeof(cb.view_proj));
    cb.eye[0] = desc.eye.x;
    cb.eye[1] = desc.eye.y;
    cb.eye[2] = desc.eye.z;
    cb.tonemap_mode = static_cast<float>(desc.tonemap_mode);
    cb.enable_auto_exposure = desc.enable_auto_exposure ? 1.f : 0.f;
    cb.auto_exposure_key = desc.auto_exposure_key;
    cb.enable_bloom = desc.enable_bloom ? 1.f : 0.f;
    cb.bloom_threshold = desc.bloom_threshold;
    cb.bloom_intensity = desc.bloom_intensity;
    cb.enable_fog = desc.enable_fog ? 1.f : 0.f;
    cb.fog_density = desc.fog_density;
    cb.fog_start = desc.fog_start;
    cb.fog_color[0] = desc.fog_color.x;
    cb.fog_color[1] = desc.fog_color.y;
    cb.fog_color[2] = desc.fog_color.z;
    // HDR scene color must always be tonemapped into the LDR swapchain.
    cb.enable_tonemap = 1.f;
    cb.enable_ssr = desc.enable_ssr ? 1.f : 0.f;
    cb.ssr_intensity = desc.ssr_intensity;
    cb.ssr_thickness = desc.ssr_thickness;
    cb.enable_dof = desc.enable_dof ? 1.f : 0.f;
    cb.dof_focus = desc.dof_focus;
    cb.dof_scale = desc.dof_scale;
    cb.enable_motion_blur = desc.enable_motion_blur ? 1.f : 0.f;
    cb.motion_blur_strength = desc.motion_blur_strength;
    std::memcpy(cb.prev_view_proj, desc.prev_view_proj.m.data(), sizeof(cb.prev_view_proj));
    cb.jitter_x = desc.jitter_x;
    cb.jitter_y = desc.jitter_y;
    cb.vignette_strength = desc.vignette_strength;
    cb.film_grain_strength = desc.film_grain_strength;
    cb.chromatic_aberration = desc.chromatic_aberration;
    cb.lens_distortion = desc.lens_distortion;
    cb.light_dirt_strength = desc.light_dirt_strength;
    cb.flare_strength = desc.flare_strength;

    const VkDeviceSize off = static_cast<VkDeviceSize>(frame_index_) * kPostUbSize;
    void* mapped = nullptr;
    if (vkMapMemory(device_, post_ub_mem_, off, sizeof(cb), 0, &mapped) != VK_SUCCESS) {
      return Status::Fail("Map post UB failed");
    }
    std::memcpy(mapped, &cb, sizeof(cb));
    vkUnmapMemory(device_, post_ub_mem_);
    return Status::Ok();
  }

  void BarrierDepth(VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout) {
    if (depth_image_ == VK_NULL_HANDLE || old_layout == new_layout) {
      depth_layout_ = new_layout;
      return;
    }
    VkImageMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout = old_layout;
    bar.newLayout = new_layout;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = depth_image_;
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    bar.subresourceRange.levelCount = 1;
    bar.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkPipelineStageFlags dst = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    if (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
        new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      bar.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
               old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      bar.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      bar.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
      src = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
      bar.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      bar.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &bar);
    depth_layout_ = new_layout;
  }

  void BarrierHistory(VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout) {
    if (history_image_ == VK_NULL_HANDLE || old_layout == new_layout) {
      history_layout_ = new_layout;
      return;
    }
    VkImageMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout = old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED : old_layout;
    bar.newLayout = new_layout;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = history_image_;
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar.subresourceRange.levelCount = 1;
    bar.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
      bar.srcAccessMask = 0;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      bar.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      src = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      src = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      bar.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      src = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
      bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
      bar.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      dst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &bar);
    history_layout_ = new_layout;
  }

  Status CopySwapchainToHistory(VkCommandBuffer cmd) {
    if (history_image_ == VK_NULL_HANDLE || image_index_ >= swapchain_images_.size()) {
      return Status::Fail("History/swapchain missing for copy");
    }
    VkImage swap = swapchain_images_[image_index_];

    VkImageMemoryBarrier bars[2]{};
    bars[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bars[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bars[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bars[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bars[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[0].image = swap;
    bars[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bars[0].subresourceRange.levelCount = 1;
    bars[0].subresourceRange.layerCount = 1;

    bars[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[1].srcAccessMask = (history_layout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                 ? VK_ACCESS_SHADER_READ_BIT
                                 : 0;
    bars[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bars[1].oldLayout = history_layout_;
    bars[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bars[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[1].image = history_image_;
    bars[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bars[1].subresourceRange.levelCount = 1;
    bars[1].subresourceRange.layerCount = 1;

    const VkPipelineStageFlags src_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(cmd, src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         2, bars);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = {width_, height_, 1};
    vkCmdCopyImage(cmd, swap, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, history_image_,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    bars[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bars[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    bars[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bars[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bars[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bars[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, bars);
    history_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return Status::Ok();
  }

  void DestroyHistoryOnly() {
    if (history_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, history_view_, nullptr);
      history_view_ = VK_NULL_HANDLE;
    }
    if (history_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, history_image_, nullptr);
      history_image_ = VK_NULL_HANDLE;
    }
    if (history_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, history_mem_, nullptr);
      history_mem_ = VK_NULL_HANDLE;
    }
    history_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    history_width_ = 0;
    history_height_ = 0;
  }

  Status EnsureHistory() {
    if (history_image_ != VK_NULL_HANDLE && history_width_ == width_ &&
        history_height_ == height_) {
      return Status::Ok();
    }
    DestroyHistoryOnly();
    const VkFormat fmt = surface_format_.format;
    if (auto st = CreateImage(width_, height_, fmt,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              history_image_, history_mem_);
        !st) {
      return st;
    }
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = history_image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vi, nullptr, &history_view_) != VK_SUCCESS) {
      return Status::Fail("history view failed");
    }
    history_width_ = width_;
    history_height_ = height_;
    history_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // Clear history once so first-frame TAA does not sample garbage.
    VkCommandBuffer cmd = BeginOneShot();
    if (!cmd) {
      return Status::Fail("BeginOneShot for history clear failed");
    }
    BarrierHistory(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue clear{{0.f, 0.f, 0.f, 1.f}};
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, history_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1,
                         &range);
    BarrierHistory(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EndOneShot(cmd);
    return Status::Ok();
  }

  Status CreatePostPipeline(const std::vector<std::uint8_t>& vs_spv,
                            const std::vector<std::uint8_t>& ps_spv) {
    if (post_render_pass_ == VK_NULL_HANDLE) {
      return Status::Fail("Post render pass missing");
    }
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule ps = VK_NULL_HANDLE;
    if (auto st = CreateShaderModule(vs_spv, vs); !st) {
      return st;
    }
    if (auto st = CreateShaderModule(ps_spv, ps); !st) {
      vkDestroyShaderModule(device_, vs, nullptr);
      return st;
    }

    VkDescriptorSetLayoutBinding binds[6]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[2].binding = 2;
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[3].binding = 3;
    binds[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binds[3].descriptorCount = 1;
    binds[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[4].binding = 4;
    binds[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    binds[4].descriptorCount = 1;
    binds[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[5].binding = 5;
    binds[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    binds[5].descriptorCount = 1;
    binds[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 6;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &post_set_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs, nullptr);
      vkDestroyShaderModule(device_, ps, nullptr);
      return Status::Fail("Create post set layout failed");
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &post_set_layout_;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &post_pipeline_layout_) != VK_SUCCESS) {
      vkDestroyShaderModule(device_, vs, nullptr);
      vkDestroyShaderModule(device_, ps, nullptr);
      return Status::Fail("Create post pipeline layout failed");
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

    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.blendEnable = VK_FALSE;
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
    gp.layout = post_pipeline_layout_;
    gp.renderPass = post_render_pass_;
    gp.subpass = 0;

    const VkResult r =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &post_pipeline_);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, ps, nullptr);
    if (r != VK_SUCCESS) {
      return Status::Fail("Create post pipeline failed: " + VkErr(r));
    }
    return Status::Ok();
  }

  Status EnsurePostDescriptors() {
    if (post_desc_set_ != VK_NULL_HANDLE && post_sampler_ != VK_NULL_HANDLE &&
        post_point_sampler_ != VK_NULL_HANDLE) {
      return Status::Ok();
    }
    if (post_sampler_ == VK_NULL_HANDLE) {
      VkSamplerCreateInfo si{};
      si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      si.magFilter = VK_FILTER_LINEAR;
      si.minFilter = VK_FILTER_LINEAR;
      si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      if (vkCreateSampler(device_, &si, nullptr, &post_sampler_) != VK_SUCCESS) {
        return Status::Fail("Create post linear sampler failed");
      }
    }
    if (post_point_sampler_ == VK_NULL_HANDLE) {
      VkSamplerCreateInfo si{};
      si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      si.magFilter = VK_FILTER_NEAREST;
      si.minFilter = VK_FILTER_NEAREST;
      si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      if (vkCreateSampler(device_, &si, nullptr, &post_point_sampler_) != VK_SUCCESS) {
        return Status::Fail("Create post point sampler failed");
      }
    }
    if (post_desc_pool_ == VK_NULL_HANDLE) {
      VkDescriptorPoolSize sizes[3]{};
      sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      sizes[0].descriptorCount = 1;
      sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      sizes[1].descriptorCount = 3;
      sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
      sizes[2].descriptorCount = 2;
      VkDescriptorPoolCreateInfo pci{};
      pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      pci.maxSets = 1;
      pci.poolSizeCount = 3;
      pci.pPoolSizes = sizes;
      if (vkCreateDescriptorPool(device_, &pci, nullptr, &post_desc_pool_) != VK_SUCCESS) {
        return Status::Fail("Create post desc pool failed");
      }
    }
    if (post_desc_set_ == VK_NULL_HANDLE) {
      VkDescriptorSetAllocateInfo ai{};
      ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      ai.descriptorPool = post_desc_pool_;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &post_set_layout_;
      if (vkAllocateDescriptorSets(device_, &ai, &post_desc_set_) != VK_SUCCESS) {
        return Status::Fail("Allocate post desc set failed");
      }
    }
    return Status::Ok();
  }

  void UpdatePostDescriptors() {
    if (post_desc_set_ == VK_NULL_HANDLE || scene_color_view_ == VK_NULL_HANDLE ||
        depth_view_ == VK_NULL_HANDLE || history_view_ == VK_NULL_HANDLE ||
        post_sampler_ == VK_NULL_HANDLE || post_point_sampler_ == VK_NULL_HANDLE ||
        post_ub_ == VK_NULL_HANDLE) {
      return;
    }
    VkDescriptorBufferInfo ub{};
    ub.buffer = post_ub_;
    ub.offset = static_cast<VkDeviceSize>(frame_index_) * kPostUbSize;
    ub.range = sizeof(PostCB);

    VkDescriptorImageInfo color{};
    color.imageView = scene_color_view_;
    color.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo depth{};
    depth.imageView = depth_view_;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo hist{};
    hist.imageView = history_view_;
    hist.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo lin{};
    lin.sampler = post_sampler_;
    VkDescriptorImageInfo point{};
    point.sampler = post_point_sampler_;

    VkWriteDescriptorSet writes[6]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = post_desc_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &ub;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = post_desc_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[1].pImageInfo = &color;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = post_desc_set_;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[2].pImageInfo = &depth;
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = post_desc_set_;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[3].pImageInfo = &hist;
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = post_desc_set_;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[4].pImageInfo = &lin;
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = post_desc_set_;
    writes[5].dstBinding = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[5].pImageInfo = &point;
    vkUpdateDescriptorSets(device_, 6, writes, 0, nullptr);
  }

  void DestroySceneColorOnly() {
    if (scene_color_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, scene_color_view_, nullptr);
      scene_color_view_ = VK_NULL_HANDLE;
    }
    if (scene_color_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, scene_color_image_, nullptr);
      scene_color_image_ = VK_NULL_HANDLE;
    }
    if (scene_color_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, scene_color_mem_, nullptr);
      scene_color_mem_ = VK_NULL_HANDLE;
    }
    scene_color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    scene_color_width_ = 0;
    scene_color_height_ = 0;
  }

  void DestroyPostResources() {
    post_stub_ready_ = false;
    DestroyPostFramebuffersOnly();
    if (post_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, post_pipeline_, nullptr);
      post_pipeline_ = VK_NULL_HANDLE;
    }
    if (post_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, post_pipeline_layout_, nullptr);
      post_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (post_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, post_set_layout_, nullptr);
      post_set_layout_ = VK_NULL_HANDLE;
    }
    if (post_render_pass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, post_render_pass_, nullptr);
      post_render_pass_ = VK_NULL_HANDLE;
    }
    post_desc_set_ = VK_NULL_HANDLE;
    if (post_desc_pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, post_desc_pool_, nullptr);
      post_desc_pool_ = VK_NULL_HANDLE;
    }
    if (post_sampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, post_sampler_, nullptr);
      post_sampler_ = VK_NULL_HANDLE;
    }
    if (post_point_sampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, post_point_sampler_, nullptr);
      post_point_sampler_ = VK_NULL_HANDLE;
    }
    if (post_ub_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, post_ub_, nullptr);
      post_ub_ = VK_NULL_HANDLE;
    }
    if (post_ub_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, post_ub_mem_, nullptr);
      post_ub_mem_ = VK_NULL_HANDLE;
    }
    DestroyHistoryOnly();
    DestroySceneColorOnly();
  }

  Status EnsureSceneColor() {
    if (scene_color_image_ != VK_NULL_HANDLE && scene_color_width_ == width_ &&
        scene_color_height_ == height_) {
      return Status::Ok();
    }
    DestroySceneColorOnly();
    if (auto st = CreateImage(width_, height_, kHdrColorFormat,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              scene_color_image_, scene_color_mem_);
        !st) {
      return st;
    }
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = scene_color_image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = kHdrColorFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vi, nullptr, &scene_color_view_) != VK_SUCCESS) {
      return Status::Fail("scene_color view failed");
    }
    scene_color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    scene_color_width_ = width_;
    scene_color_height_ = height_;
    return RecreateHdrFramebuffer();
  }

  Status CaptureSceneColorIntermediate(VkCommandBuffer cmd) {
    if (auto st = EnsureSceneColor(); !st) {
      return st;
    }
    if (pass_active_) {
      vkCmdEndRenderPass(cmd);
      pass_active_ = false;
      present_pass_active_ = false;
      present_pass_load_ = false;
      // Lit/HDR pass leaves depth in DEPTH_STENCIL_ATTACHMENT_OPTIMAL.
      depth_layout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // Lit already wrote HDR into scene_color_. Transition for post sampling.
    VkImageMemoryBarrier bars[2]{};
    bars[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bars[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bars[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[0].image = scene_color_image_;
    bars[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bars[0].subresourceRange.levelCount = 1;
    bars[0].subresourceRange.layerCount = 1;

    bars[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[1].oldLayout = depth_layout_ == VK_IMAGE_LAYOUT_UNDEFINED
                             ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                             : depth_layout_;
    bars[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    bars[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bars[1].image = depth_image_;
    bars[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    bars[1].subresourceRange.levelCount = 1;
    bars[1].subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                         bars);
    scene_color_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depth_layout_ = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
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

    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    if (vkCreateRenderPass(device_, &rpci, nullptr, &shadow_render_pass_load_) != VK_SUCCESS) {
      return Status::Fail("Create shadow LOAD render pass failed");
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

    // Dedicated local-shadow atlas (do not share CSM depth — that stomped cascades).
    if (auto st = CreateImage(kLocalShadowMapSize, kLocalShadowMapSize, VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT,
                              local_shadow_image_, local_shadow_mem_);
        !st) {
      return st;
    }
    {
      VkImageViewCreateInfo lvi{};
      lvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      lvi.image = local_shadow_image_;
      lvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      lvi.format = VK_FORMAT_D32_SFLOAT;
      lvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      lvi.subresourceRange.levelCount = 1;
      lvi.subresourceRange.layerCount = 1;
      if (vkCreateImageView(device_, &lvi, nullptr, &local_shadow_view_) != VK_SUCCESS) {
        return Status::Fail("Create local shadow view failed");
      }
    }
    local_shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    {
      VkCommandBuffer cmd = BeginOneShot();
      BarrierLocalShadowImage(cmd, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
      EndOneShot(cmd);
    }
    {
      VkFramebufferCreateInfo lfi{};
      lfi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      lfi.renderPass = shadow_render_pass_;
      lfi.attachmentCount = 1;
      lfi.pAttachments = &local_shadow_view_;
      lfi.width = kLocalShadowMapSize;
      lfi.height = kLocalShadowMapSize;
      lfi.layers = 1;
      if (vkCreateFramebuffer(device_, &lfi, nullptr, &local_shadow_framebuffer_) != VK_SUCCESS) {
        return Status::Fail("Create local shadow framebuffer failed");
      }
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.compareEnable = VK_TRUE;
    sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (vkCreateSampler(device_, &sci, nullptr, &shadow_sampler_) != VK_SUCCESS) {
      return Status::Fail("Create shadow comparison sampler failed");
    }

    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    rs.rasterizerDiscardEnable = VK_FALSE;
    // Depth bias: keep slope matched to D3D12 (SlopeScaledDepthBias=2).
    // Constant factor uses Vulkan's r-scaled units (not D3D integer DepthBias=1500).
    // 1.25 was under-biased vs D3D on D32 → more acne / softer-looking contacts.
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 2.5f;
    rs.depthBiasClamp = 0.f;
    rs.depthBiasSlopeFactor = 2.0f;

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
    const std::uint32_t indices[] = {
        0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
        12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };
    return UploadLitGeometry(0, std::span<const LitVertex>(verts, 24),
                             std::span<const std::uint32_t>(indices, 36));
  }

  Status CreateLitBuffersAndDescriptors() {
    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (auto st = CreateBuffer(kFrameUbSize * kFramesInFlight, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               host, frame_ub_, frame_ub_mem_);
        !st) {
      return st;
    }
    if (auto st = CreateBuffer(kUniformAlign * kShadowVpSlots * kFramesInFlight,
                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, shadow_frame_ub_,
                               shadow_frame_ub_mem_);
        !st) {
      return st;
    }
    if (auto st = CreateBuffer(kUniformAlign * kMaxLitDraws * kFramesInFlight,
                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, object_ub_, object_ub_mem_);
        !st) {
      return st;
    }

    {
      VkSamplerCreateInfo si{};
      si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      si.magFilter = VK_FILTER_LINEAR;
      si.minFilter = VK_FILTER_LINEAR;
      si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
      si.maxLod = 16.f;
      if (vkCreateSampler(device_, &si, nullptr, &lit_linear_sampler_) != VK_SUCCESS) {
        return Status::Fail("Create lit linear sampler failed");
      }
    }

    VkDescriptorPoolSize sizes[4]{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    sizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4};
    sizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10};
    sizes[3] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 2;
    pci.poolSizeCount = 4;
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

    // Default 1x1 irradiance cube so binding 3 is always valid.
    {
      std::array<std::uint8_t, 6 * 4> faces{};
      for (int i = 0; i < 6; ++i) {
        faces[static_cast<std::size_t>(i * 4 + 0)] = 40;
        faces[static_cast<std::size_t>(i * 4 + 1)] = 45;
        faces[static_cast<std::size_t>(i * 4 + 2)] = 55;
        faces[static_cast<std::size_t>(i * 4 + 3)] = 255;
      }
      if (auto st = UploadIblCubemapGpu(faces.data(), 1, true); !st) {
        return st;
      }
    }

    // Default albedo/ORM (slot0/1), prefilter cube, probe cube, BRDF LUT so bindings stay valid.
    {
      const std::uint8_t white[4] = {255, 255, 255, 255};
      const std::uint8_t grey[4] = {255, 128, 128, 255};  // AO / rough / metal
      if (auto st = UploadRgba2D(lit_albedo_[0], white, 1, 1, 4, lit_linear_sampler_); !st) {
        return st;
      }
      if (auto st = UploadRgba2D(lit_orm_[0], grey, 1, 1, 5, lit_linear_sampler_); !st) {
        return st;
      }
      if (auto st = UploadRgba2D(lit_albedo_[1], white, 1, 1, 6, lit_linear_sampler_); !st) {
        return st;
      }
      if (auto st = UploadRgba2D(lit_orm_[1], grey, 1, 1, 7, lit_linear_sampler_); !st) {
        return st;
      }
      std::array<std::uint8_t, 6 * 4> pref{};
      for (int i = 0; i < 6; ++i) {
        pref[static_cast<std::size_t>(i * 4 + 0)] = 32;
        pref[static_cast<std::size_t>(i * 4 + 1)] = 32;
        pref[static_cast<std::size_t>(i * 4 + 2)] = 36;
        pref[static_cast<std::size_t>(i * 4 + 3)] = 255;
      }
      if (auto st = UploadCubemapTo(ibl_prefilter_image_, ibl_prefilter_mem_, ibl_prefilter_view_,
                                    pref.data(), 1);
          !st) {
        return st;
      }
      UpdateLitCombinedBinding(8, ibl_prefilter_view_, lit_linear_sampler_);
      std::array<std::uint8_t, 6 * 4> probe{};
      for (int i = 0; i < 6; ++i) {
        probe[static_cast<std::size_t>(i * 4 + 0)] = 40;
        probe[static_cast<std::size_t>(i * 4 + 1)] = 50;
        probe[static_cast<std::size_t>(i * 4 + 2)] = 70;
        probe[static_cast<std::size_t>(i * 4 + 3)] = 255;
      }
      if (auto st = UploadCubemapTo(reflection_probe_image_, reflection_probe_mem_,
                                    reflection_probe_view_, probe.data(), 1);
          !st) {
        return st;
      }
      UpdateLitCombinedBinding(12, reflection_probe_view_, lit_linear_sampler_);
      const std::uint8_t lut[4] = {255, 255, 0, 255};
      if (auto st = UploadRgba2D(ibl_lut_, lut, 1, 1, 9, lit_linear_sampler_); !st) {
        return st;
      }
    }

    if (local_shadow_view_ != VK_NULL_HANDLE && shadow_sampler_ != VK_NULL_HANDLE) {
      if (local_shadow_layout_ != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        VkCommandBuffer cmd = BeginOneShot();
        BarrierLocalShadowImage(cmd, local_shadow_layout_,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        EndOneShot(cmd);
      }
      UpdateLitCombinedBinding(10, local_shadow_view_, shadow_sampler_,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    VkDescriptorImageInfo ibl_info{};
    ibl_info.sampler = ibl_sampler_;
    ibl_info.imageView = ibl_cube_view_;
    ibl_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo shadow_frame_info{};
    shadow_frame_info.buffer = shadow_frame_ub_;
    shadow_frame_info.offset = 0;
    shadow_frame_info.range = sizeof(ShadowFrameGpu);

    // Dummy 1-matrix SSBO so binding 11 is always valid before first Upload.
    {
      const VkMemoryPropertyFlags host_vis =
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      const VkDeviceSize dummy_bytes = sizeof(Mat4);
      for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (instance_bufs_[i] == VK_NULL_HANDLE) {
          if (auto st = CreateBuffer(dummy_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_vis,
                                     instance_bufs_[i], instance_buf_mems_[i]);
              !st) {
            return st;
          }
          instance_buf_bytes_[i] = dummy_bytes;
          Mat4 id = Mat4::Identity();
          void* mapped = nullptr;
          if (vkMapMemory(device_, instance_buf_mems_[i], 0, dummy_bytes, 0, &mapped) ==
              VK_SUCCESS) {
            std::memcpy(mapped, id.m.data(), sizeof(id.m));
            vkUnmapMemory(device_, instance_buf_mems_[i]);
          }
        }
      }
    }
    VkDescriptorBufferInfo instance_info{};
    instance_info.buffer = instance_bufs_[0];
    instance_info.offset = 0;
    instance_info.range = sizeof(Mat4);

    VkWriteDescriptorSet writes[7]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = lit_desc_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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
    writes[3].dstSet = lit_desc_set_;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &ibl_info;
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = shadow_desc_set_;
    writes[4].dstBinding = 0;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[4].pBufferInfo = &shadow_frame_info;
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = shadow_desc_set_;
    writes[5].dstBinding = 1;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[5].pBufferInfo = &obj_info;
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = lit_desc_set_;
    writes[6].dstBinding = 11;
    writes[6].descriptorCount = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].pBufferInfo = &instance_info;
    vkUpdateDescriptorSets(device_, 7, writes, 0, nullptr);
    return Status::Ok();
  }

  void UpdateCullDescriptors() {
    if (cull_desc_set_ == VK_NULL_HANDLE || indirect_args_buf_ == VK_NULL_HANDLE ||
        cull_compact_buf_ == VK_NULL_HANDLE) {
      return;
    }
    VkDescriptorBufferInfo args_info{};
    args_info.buffer = indirect_args_buf_;
    args_info.offset = 0;
    args_info.range = VK_WHOLE_SIZE;
    VkDescriptorBufferInfo compact_info{};
    compact_info.buffer = cull_compact_buf_;
    compact_info.offset = 0;
    compact_info.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = cull_desc_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &args_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = cull_desc_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &compact_info;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
  }

  void DestroyCullCompute() {
    cull_ready_ = false;
    cull_desc_set_ = VK_NULL_HANDLE;
    if (cull_desc_pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, cull_desc_pool_, nullptr);
      cull_desc_pool_ = VK_NULL_HANDLE;
    }
    if (cull_pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, cull_pipeline_, nullptr);
      cull_pipeline_ = VK_NULL_HANDLE;
    }
    if (cull_pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, cull_pipeline_layout_, nullptr);
      cull_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (cull_set_layout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, cull_set_layout_, nullptr);
      cull_set_layout_ = VK_NULL_HANDLE;
    }
    if (cull_compact_buf_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, cull_compact_buf_, nullptr);
      cull_compact_buf_ = VK_NULL_HANDLE;
    }
    if (cull_compact_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, cull_compact_mem_, nullptr);
      cull_compact_mem_ = VK_NULL_HANDLE;
    }
    cull_compact_bytes_ = 0;
  }

  void DestroyIndirectArgsBuffers(bool keep_uploads) {
    if (indirect_args_buf_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, indirect_args_buf_, nullptr);
      indirect_args_buf_ = VK_NULL_HANDLE;
    }
    if (indirect_args_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, indirect_args_mem_, nullptr);
      indirect_args_mem_ = VK_NULL_HANDLE;
    }
    indirect_args_bytes_ = 0;
    if (!keep_uploads) {
      for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (indirect_args_upload_[i] != VK_NULL_HANDLE) {
          vkDestroyBuffer(device_, indirect_args_upload_[i], nullptr);
          indirect_args_upload_[i] = VK_NULL_HANDLE;
        }
        if (indirect_args_upload_mem_[i] != VK_NULL_HANDLE) {
          vkFreeMemory(device_, indirect_args_upload_mem_[i], nullptr);
          indirect_args_upload_mem_[i] = VK_NULL_HANDLE;
        }
        indirect_args_upload_bytes_[i] = 0;
      }
      if (indirect_zero_upload_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, indirect_zero_upload_, nullptr);
        indirect_zero_upload_ = VK_NULL_HANDLE;
      }
      if (indirect_zero_upload_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, indirect_zero_upload_mem_, nullptr);
        indirect_zero_upload_mem_ = VK_NULL_HANDLE;
      }
    }
  }

  void DestroyLitResources() {
    lit_ready_ = false;
    shadow_pass_active_ = false;
    bound_cascade_ = -1;
    DestroyCullCompute();
    DestroyIndirectArgsBuffers(/*keep_uploads=*/false);
    DestroyPostResources();
    DestroySkyResources();
    DestroyUiResources();
    DestroyQuadResources();
    DestroyDebugResources();
    DestroyIblCube();
    DestroyPrefilterCube();
    DestroyReflectionProbeCube();
    DestroyTex2D(ibl_lut_);
    for (int i = 0; i < 2; ++i) {
      DestroyTex2D(lit_albedo_[i]);
      DestroyTex2D(lit_orm_[i]);
    }
    if (lit_linear_sampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, lit_linear_sampler_, nullptr);
      lit_linear_sampler_ = VK_NULL_HANDLE;
    }
    if (ibl_sampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, ibl_sampler_, nullptr);
      ibl_sampler_ = VK_NULL_HANDLE;
    }
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
    if (local_shadow_framebuffer_ != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, local_shadow_framebuffer_, nullptr);
      local_shadow_framebuffer_ = VK_NULL_HANDLE;
    }
    if (shadow_render_pass_load_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, shadow_render_pass_load_, nullptr);
      shadow_render_pass_load_ = VK_NULL_HANDLE;
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
    if (local_shadow_view_ != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, local_shadow_view_, nullptr);
      local_shadow_view_ = VK_NULL_HANDLE;
    }
    if (local_shadow_image_ != VK_NULL_HANDLE) {
      vkDestroyImage(device_, local_shadow_image_, nullptr);
      local_shadow_image_ = VK_NULL_HANDLE;
    }
    if (local_shadow_mem_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, local_shadow_mem_, nullptr);
      local_shadow_mem_ = VK_NULL_HANDLE;
    }
    local_shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    shadow_desc_set_ = VK_NULL_HANDLE;

    if (lit_pipeline_transparent_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, lit_pipeline_transparent_, nullptr);
      lit_pipeline_transparent_ = VK_NULL_HANDLE;
    }
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
    for (int i = 0; i < kMaxMeshSlots; ++i) {
      DestroyMeshSlot(mesh_slots_[static_cast<std::size_t>(i)]);
    }
    destroy_buf(frame_ub_, frame_ub_mem_);
    destroy_buf(shadow_frame_ub_, shadow_frame_ub_mem_);
    destroy_buf(object_ub_, object_ub_mem_);
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
      destroy_buf(instance_bufs_[i], instance_buf_mems_[i]);
      instance_buf_bytes_[i] = 0;
    }

    if (render_pass_load_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, render_pass_load_, nullptr);
      render_pass_load_ = VK_NULL_HANDLE;
    }
    if (render_pass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, render_pass_, nullptr);
      render_pass_ = VK_NULL_HANDLE;
    }
    DestroyPresentRenderPasses();
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
  VkBuffer color_readback_buf_ = VK_NULL_HANDLE;
  VkDeviceMemory color_readback_mem_ = VK_NULL_HANDLE;
  VkDeviceSize color_readback_size_ = 0;
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
  bool present_pass_active_ = false;
  bool present_pass_load_ = false;
  bool used_graphics_ = false;
  ColorRgba clear_color_{0.f, 0.f, 0.f, 1.f};

  bool lit_ready_ = false;
  bool enable_validation_ = false;
  int adapter_index_ = -1;
  bool vsync_ = false;
  bool vsync_dirty_ = false;
  bool post_stub_ready_ = false;
  bool post_resolve_warned_ = false;
  bool ibl_upload_logged_ = false;
  bool local_shadow_pass_active_ = false;
  float post_exposure_ = 1.f;
  int post_tonemap_mode_ = 2;
  std::vector<std::uint32_t> indirect_args_cpu_;
  std::uint32_t indirect_fallback_instances_ = 0;
  VkBuffer indirect_args_buf_ = VK_NULL_HANDLE;
  VkDeviceMemory indirect_args_mem_ = VK_NULL_HANDLE;
  VkDeviceSize indirect_args_bytes_ = 0;
  std::array<VkBuffer, kFramesInFlight> indirect_args_upload_{};
  std::array<VkDeviceMemory, kFramesInFlight> indirect_args_upload_mem_{};
  std::array<VkDeviceSize, kFramesInFlight> indirect_args_upload_bytes_{};
  VkBuffer indirect_zero_upload_ = VK_NULL_HANDLE;
  VkDeviceMemory indirect_zero_upload_mem_ = VK_NULL_HANDLE;

  bool cull_ready_ = false;
  VkDescriptorSetLayout cull_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout cull_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline cull_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool cull_desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet cull_desc_set_ = VK_NULL_HANDLE;
  VkBuffer cull_compact_buf_ = VK_NULL_HANDLE;
  VkDeviceMemory cull_compact_mem_ = VK_NULL_HANDLE;
  VkDeviceSize cull_compact_bytes_ = 0;

  VkPipeline post_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout post_pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout post_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool post_desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet post_desc_set_ = VK_NULL_HANDLE;
  VkSampler post_sampler_ = VK_NULL_HANDLE;
  VkSampler post_point_sampler_ = VK_NULL_HANDLE;
  VkBuffer post_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory post_ub_mem_ = VK_NULL_HANDLE;
  VkRenderPass post_render_pass_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> post_framebuffers_;
  VkImage history_image_ = VK_NULL_HANDLE;
  VkDeviceMemory history_mem_ = VK_NULL_HANDLE;
  VkImageView history_view_ = VK_NULL_HANDLE;
  VkImageLayout history_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  std::uint32_t history_width_ = 0;
  std::uint32_t history_height_ = 0;
  VkImage scene_color_image_ = VK_NULL_HANDLE;
  VkDeviceMemory scene_color_mem_ = VK_NULL_HANDLE;
  VkImageView scene_color_view_ = VK_NULL_HANDLE;
  VkImageLayout scene_color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  std::uint32_t scene_color_width_ = 0;
  std::uint32_t scene_color_height_ = 0;
  bool post_resolved_this_frame_ = false;
  int ibl_lut_w_ = 0;
  int ibl_lut_h_ = 0;
  VkImage ibl_cube_image_ = VK_NULL_HANDLE;
  VkDeviceMemory ibl_cube_mem_ = VK_NULL_HANDLE;
  VkImageView ibl_cube_view_ = VK_NULL_HANDLE;
  VkSampler ibl_sampler_ = VK_NULL_HANDLE;
  VkImage ibl_prefilter_image_ = VK_NULL_HANDLE;
  VkDeviceMemory ibl_prefilter_mem_ = VK_NULL_HANDLE;
  VkImageView ibl_prefilter_view_ = VK_NULL_HANDLE;
  VkImage reflection_probe_image_ = VK_NULL_HANDLE;
  VkDeviceMemory reflection_probe_mem_ = VK_NULL_HANDLE;
  VkImageView reflection_probe_view_ = VK_NULL_HANDLE;
  Tex2DGpu ibl_lut_{};
  Tex2DGpu lit_albedo_[2]{};
  Tex2DGpu lit_orm_[2]{};
  VkSampler lit_linear_sampler_ = VK_NULL_HANDLE;
  bool shadow_pass_active_ = false;
  int bound_cascade_ = -1;
  int bound_shadow_vp_slot_ = 0;
  std::uint32_t lit_draws_this_frame_ = 0;
  std::uint32_t shadow_draws_this_pass_ = 0;
  FrameLighting lighting_{};

  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_load_ = VK_NULL_HANDLE;
  VkRenderPass present_render_pass_ = VK_NULL_HANDLE;
  VkRenderPass present_render_pass_load_ = VK_NULL_HANDLE;
  VkFramebuffer hdr_framebuffer_ = VK_NULL_HANDLE;
  VkImage depth_image_ = VK_NULL_HANDLE;
  VkDeviceMemory depth_mem_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  VkImageLayout depth_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  std::vector<VkFramebuffer> framebuffers_;

  VkDescriptorSetLayout lit_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout lit_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline lit_pipeline_ = VK_NULL_HANDLE;
  VkPipeline lit_pipeline_transparent_ = VK_NULL_HANDLE;
  VkDescriptorPool lit_desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet lit_desc_set_ = VK_NULL_HANDLE;
  bool depth_clamp_enabled_ = false;

  VkImage shadow_image_ = VK_NULL_HANDLE;
  VkDeviceMemory shadow_mem_ = VK_NULL_HANDLE;
  VkImageView shadow_view_ = VK_NULL_HANDLE;
  VkSampler shadow_sampler_ = VK_NULL_HANDLE;
  VkRenderPass shadow_render_pass_ = VK_NULL_HANDLE;
  VkRenderPass shadow_render_pass_load_ = VK_NULL_HANDLE;
  VkFramebuffer shadow_framebuffer_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout shadow_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout shadow_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorSet shadow_desc_set_ = VK_NULL_HANDLE;
  VkImageLayout shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage local_shadow_image_ = VK_NULL_HANDLE;
  VkDeviceMemory local_shadow_mem_ = VK_NULL_HANDLE;
  VkImageView local_shadow_view_ = VK_NULL_HANDLE;
  VkFramebuffer local_shadow_framebuffer_ = VK_NULL_HANDLE;
  VkImageLayout local_shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

  std::array<MeshSlotGpu, kMaxMeshSlots> mesh_slots_{};
  VkBuffer frame_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory frame_ub_mem_ = VK_NULL_HANDLE;
  VkBuffer shadow_frame_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory shadow_frame_ub_mem_ = VK_NULL_HANDLE;
  VkBuffer object_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory object_ub_mem_ = VK_NULL_HANDLE;
  std::array<VkBuffer, kFramesInFlight> instance_bufs_{};
  std::array<VkDeviceMemory, kFramesInFlight> instance_buf_mems_{};
  std::array<VkDeviceSize, kFramesInFlight> instance_buf_bytes_{};

  // Skybox
  bool sky_ready_ = false;
  bool sky_uploaded_ = false;
  VkPipeline sky_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout sky_pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout sky_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool sky_desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet sky_desc_set_ = VK_NULL_HANDLE;
  VkBuffer sky_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory sky_ub_mem_ = VK_NULL_HANDLE;
  VkImage sky_cube_image_ = VK_NULL_HANDLE;
  VkDeviceMemory sky_cube_mem_ = VK_NULL_HANDLE;
  VkImageView sky_cube_view_ = VK_NULL_HANDLE;
  VkSampler sky_cube_sampler_ = VK_NULL_HANDLE;

  // UI
  bool ui_ready_ = false;
  bool ui_font_uploaded_ = false;
  VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout ui_pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout ui_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool ui_desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet ui_desc_set_ = VK_NULL_HANDLE;
  VkSampler ui_sampler_ = VK_NULL_HANDLE;
  VkBuffer ui_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory ui_ub_mem_ = VK_NULL_HANDLE;
  std::array<VkBuffer, kFramesInFlight> ui_vb_{};
  std::array<VkDeviceMemory, kFramesInFlight> ui_vb_mem_{};
  std::array<VkBuffer, kFramesInFlight> ui_ib_{};
  std::array<VkDeviceMemory, kFramesInFlight> ui_ib_mem_{};
  std::uint32_t ui_vb_capacity_ = 0;
  std::uint32_t ui_ib_capacity_ = 0;
  Tex2DGpu ui_font_{};

  // Screen quads
  bool quad_ready_ = false;
  VkPipeline quad_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout quad_pipeline_layout_ = VK_NULL_HANDLE;
  std::array<VkBuffer, kFramesInFlight> quad_vb_{};
  std::array<VkDeviceMemory, kFramesInFlight> quad_vb_mem_{};
  std::uint32_t quad_vb_capacity_ = 0;

  // Debug lines
  bool debug_ready_ = false;
  VkPipeline debug_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout debug_pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout debug_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool debug_desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet debug_desc_set_ = VK_NULL_HANDLE;
  VkBuffer debug_ub_ = VK_NULL_HANDLE;
  VkDeviceMemory debug_ub_mem_ = VK_NULL_HANDLE;
  std::array<VkBuffer, kFramesInFlight> debug_vb_{};
  std::array<VkDeviceMemory, kFramesInFlight> debug_vb_mem_{};
  std::uint32_t debug_vb_capacity_ = 0;
};

}  // namespace

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
