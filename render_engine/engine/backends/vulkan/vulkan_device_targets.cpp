#include "vulkan_device_internal.h"

namespace engine::rhi {

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
