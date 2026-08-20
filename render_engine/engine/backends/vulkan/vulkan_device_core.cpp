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

Status VulkanDevice::CreateInstance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "render_engine";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "render_engine";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_1;

#if defined(_WIN32)
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
#elif defined(__linux__)
    // W16: enable both xlib + wayland; unused surface ext is fine if present.
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
                                                VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
#else
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME};
#endif
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
#if defined(__linux__)
    ci.enabledExtensionCount = 3;
#else
    ci.enabledExtensionCount = 2;
#endif
    ci.ppEnabledExtensionNames = exts;
    if (enable_validation_) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = layers;
    }

    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS && enable_validation_) {
        LogWarn("Vulkan validation layers unavailable — retry without");
        ci.enabledLayerCount = 0;
        ci.ppEnabledLayerNames = nullptr;
        r = vkCreateInstance(&ci, nullptr, &instance_);
    }
    if (r != VK_SUCCESS) {
        return Status::Fail("vkCreateInstance failed: " + VkErr(r));
    }
    return Status::Ok();
}

Status VulkanDevice::CreateSurface() {
#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = hwnd_;
    const VkResult r = vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_);
    if (r != VK_SUCCESS) {
        return Status::Fail("vkCreateWin32SurfaceKHR failed: " + VkErr(r));
    }
    return Status::Ok();
#elif defined(__linux__)
    if (wayland_) {
#if defined(ENGINE_HAS_WAYLAND)
        VkWaylandSurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        ci.display = static_cast<wl_display*>(wayland_->display);
        ci.surface = static_cast<wl_surface*>(wayland_->surface);
        const VkResult r = vkCreateWaylandSurfaceKHR(instance_, &ci, nullptr, &surface_);
        if (r != VK_SUCCESS) {
            return Status::Fail("vkCreateWaylandSurfaceKHR failed: " + VkErr(r));
        }
        return Status::Ok("wayland-surface");
#else
        return Status::Fail(ErrorCode::Unavailable, "CreateSurface: ENGINE_HAS_WAYLAND off");
#endif
    }
    return TryCreateXlibSurface(instance_, x11_->display, x11_->window, &surface_);
#else
    return Status::Fail(ErrorCode::Unavailable, "CreateSurface: unsupported platform");
#endif
}

Status VulkanDevice::PickPhysicalDevice() {
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

Status VulkanDevice::CreateLogicalDevice() {
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

    // Probe descriptor-indexing (capability doc only; Feature bindless stays unset / SKIP).
    descriptor_indexing_available_ = false;
    {
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(physical_, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> dexts(ext_count);
        vkEnumerateDeviceExtensionProperties(physical_, nullptr, &ext_count, dexts.data());
        for (const auto& e : dexts) {
            if (std::strcmp(e.extensionName, "VK_EXT_descriptor_indexing") == 0 ||
                    std::strcmp(e.extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0) {
                descriptor_indexing_available_ = true;
                break;
            }
        }
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

Status VulkanDevice::CreateSwapchain() {
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

void VulkanDevice::DestroySwapchainViews() {
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

void VulkanDevice::DestroySwapchain() {
    DestroyFramebuffersOnly();
    DestroyPostFramebuffersOnly();
    DestroyDepthOnly();
    DestroySwapchainViews();
}

Status VulkanDevice::RecreateSwapchain() {
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

Status VulkanDevice::CreateImage(std::uint32_t width, std::uint32_t height, VkFormat format,
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

Status VulkanDevice::CreateRenderPass() {
    if (auto st = CreateLitRenderPass(); !st) {
        return st;
    }
    return CreatePresentRenderPass();
}

void VulkanDevice::DestroyPresentRenderPasses() {
    if (present_render_pass_load_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, present_render_pass_load_, nullptr);
        present_render_pass_load_ = VK_NULL_HANDLE;
    }
    if (present_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, present_render_pass_, nullptr);
        present_render_pass_ = VK_NULL_HANDLE;
    }
}

Status VulkanDevice::CreatePresentRenderPass() {
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

void VulkanDevice::DestroyDepthOnly() {
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

Status VulkanDevice::CreateDepthResources() {
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

Status VulkanDevice::CreateShaderModule(const std::vector<std::uint8_t>& spirv, VkShaderModule& out) {
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

void VulkanDevice::BarrierDepth(VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout) {
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

void VulkanDevice::BarrierHistory(VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout) {
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

Status VulkanDevice::CopySwapchainToHistory(VkCommandBuffer cmd) {
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

void VulkanDevice::DestroyHistoryOnly() {
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

Status VulkanDevice::EnsureHistory() {
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

void VulkanDevice::DestroySceneColorOnly() {
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

Status VulkanDevice::EnsureSceneColor() {
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

Status VulkanDevice::CaptureSceneColorIntermediate(VkCommandBuffer cmd) {
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

}  // namespace engine::rhi
