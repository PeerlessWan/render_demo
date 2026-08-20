#include "vulkan_device_internal.h"

namespace engine::rhi {

Status VulkanDevice::CreateAndUploadRgba2D(Tex2DGpu& out, const std::uint8_t* rgba, int width, int height) {
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

Status VulkanDevice::UploadRgba2D(Tex2DGpu& out, const std::uint8_t* rgba, int width, int height,
                                        std::uint32_t binding, VkSampler sampler) {
    if (device_ == VK_NULL_HANDLE) {
        return Status::Fail("Device not ready");
    }
    // Fence-wait prior queue work before retiring the old image (not DeviceWaitIdle).
    WaitGpuSubmitted();
    DestroyTex2D(out);
    if (auto st = CreateAndUploadRgba2D(out, rgba, width, height); !st) {
        return st;
    }
    if (sampler != VK_NULL_HANDLE) {
        UpdateLitCombinedBinding(binding, out.view, sampler);
    }
    return Status::Ok();
}

Status VulkanDevice::UploadCubemapTo(VkImage& image, VkDeviceMemory& mem, VkImageView& view,
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

Status VulkanDevice::UploadIblCubemapGpu(const std::uint8_t* rgba_faces, int face_size, bool bind_as_irradiance) {
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

Status VulkanDevice::AcceptIblUploadOnce() {
    if (!ibl_upload_logged_) {
        LogInfo("Vulkan IBL upload accepted (sampling parity TBD)");
        ibl_upload_logged_ = true;
    }
    return Status::Ok();
}

Status VulkanDevice::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
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

void VulkanDevice::DestroyFramebuffersOnly() {
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

Status VulkanDevice::RecreateHdrFramebuffer() {
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

Status VulkanDevice::CreateFramebuffers() {
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

Status VulkanDevice::CreateLitBuffersAndDescriptors() {
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
    sizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12};
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
        // W20 L0 defaults: black GI atlas + white soft-shadow mask (flags off → no visual change).
        const std::uint8_t black[4] = {0, 0, 0, 255};
        const std::uint8_t white_mask[4] = {255, 255, 255, 255};
        if (auto st = UploadRgba2D(probe_gi_atlas_, black, 1, 1, 13, lit_linear_sampler_); !st) {
            return st;
        }
        if (auto st = UploadRgba2D(soft_shadow_mask_, white_mask, 1, 1, 14, lit_linear_sampler_);
                !st) {
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

}  // namespace engine::rhi
