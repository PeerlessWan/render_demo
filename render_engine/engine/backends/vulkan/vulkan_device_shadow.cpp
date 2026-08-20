#include "vulkan_device_internal.h"

namespace engine::rhi {

Status VulkanDevice::BeginShadowPass() {
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

Status VulkanDevice::BindShadowCascade(int cascade_index) {
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

Status VulkanDevice::DrawShadowCubes(std::span<const LitDrawItem> items) {
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

Status VulkanDevice::EndShadowPass() {
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

Status VulkanDevice::BeginLocalShadowPass() {
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

Status VulkanDevice::BindLocalShadowTile(int tile) {
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

Status VulkanDevice::EndLocalShadowPass() {
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

void VulkanDevice::BarrierShadowImage(VkCommandBuffer cmd, VkImageLayout old_layout,
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

void VulkanDevice::BarrierLocalShadowImage(VkCommandBuffer cmd, VkImageLayout old_layout,
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

Status VulkanDevice::ImmediateTransitionShadow(VkImageLayout new_layout) {
    VkCommandBuffer cmd = BeginOneShot();
    if (cmd == VK_NULL_HANDLE) {
        return Status::Fail("Allocate transition cmd failed");
    }
    BarrierShadowImage(cmd, shadow_layout_, new_layout);
    EndOneShot(cmd);
    return Status::Ok();
}

Status VulkanDevice::CreateShadowResources(const std::vector<std::uint8_t>& shadow_vs_spv) {
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

}  // namespace engine::rhi
