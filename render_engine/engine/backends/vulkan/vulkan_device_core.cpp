#include "vulkan_device_internal.h"

namespace engine::rhi {

Status VulkanDevice::Init(const DeviceDesc& desc) {
    if (!desc.native_window || desc.width == 0 || desc.height == 0) {
        return Status::Fail(ErrorCode::InvalidArgument, "Invalid DeviceDesc for Vulkan");
    }
#if defined(_WIN32)
    hwnd_ = static_cast<HWND>(desc.native_window);
#elif defined(__linux__)
    {
        const auto* kind = static_cast<const platform::LinuxNativeKind*>(desc.native_window);
        if (kind && *kind == platform::LinuxNativeKind::Wayland) {
            wayland_ = static_cast<const platform::linux_wayland::WaylandNative*>(desc.native_window);
            if (!wayland_ || !wayland_->display || !wayland_->surface) {
                return Status::Fail(ErrorCode::InvalidArgument,
                                                        "Invalid DeviceDesc for Vulkan (need WaylandNative* display+surface)");
            }
        } else {
            x11_ = static_cast<const platform::linux_x11::X11Native*>(desc.native_window);
            if (!x11_ || !x11_->display || !x11_->window) {
                return Status::Fail(ErrorCode::InvalidArgument,
                                                        "Invalid DeviceDesc for Vulkan (need X11Native* with display+window)");
            }
        }
    }
#else
    return Status::Fail(ErrorCode::Unavailable, "Vulkan surface platform unsupported");
#endif
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

    // W12/W13 ADR 0039: when VK_EXT_descriptor_indexing is present, expose Feature
    // bindless (capability). Hot path still Feature-gated (default OFF), same as D3D12.
    if (descriptor_indexing_available_) {
        bindless_capable_ = true;
        engine::SetFeatureOverride("bindless", true);
        LogInfo("Vulkan bindless Feature path enabled (descriptor-indexing); "
                        "bindless_hot_path opt-in (default OFF; W16 ADR 0040)");
    } else {
        bindless_capable_ = false;
        LogInfo("Vulkan bindless SKIP (no VK_EXT_descriptor_indexing; classic only)");
    }
#if defined(_WIN32)
    LogInfo("Vulkan device ready (Win32 surface + swapchain clear)");
#elif defined(__linux__)
    LogInfo("Vulkan device ready (Xlib surface + swapchain clear)");
#endif
    return Status::Ok();
}

VulkanDevice::~VulkanDevice() {
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

DeviceApiKind VulkanDevice::api_kind() const { return DeviceApiKind::Vulkan; }

std::uint32_t VulkanDevice::width() const { return width_; }

std::uint32_t VulkanDevice::height() const { return height_; }

void VulkanDevice::SetVSync(bool enabled) {
    if (vsync_ == enabled) {
        return;
    }
    vsync_ = enabled;
    vsync_dirty_ = true;
}

[[nodiscard]] bool VulkanDevice::vsync() const { return vsync_; }

Status VulkanDevice::BeginFrame() {
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

Status VulkanDevice::Clear(const ColorRgba& color) {
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

Status VulkanDevice::Present() {
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

Status VulkanDevice::Resize(std::uint32_t width, std::uint32_t height) {
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

Status VulkanDevice::DrawSimpleMesh() {
    return Status::Fail("Vulkan lit not ready");
}

Status VulkanDevice::SetupSimpleMesh(const SimpleMeshShaders&) {
    return Status::Fail("Vulkan lit not ready");
}

Status VulkanDevice::ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) {
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

void VulkanDevice::DestroyTex2D(Tex2DGpu& tex) {
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

void VulkanDevice::DestroyPrefilterCube() {
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

Status VulkanDevice::BeginPresentRenderPass(const ColorRgba& color, bool load_contents) {
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

}  // namespace engine::rhi
