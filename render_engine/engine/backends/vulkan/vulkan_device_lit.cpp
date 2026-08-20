#include "vulkan_device_internal.h"

namespace engine::rhi {

Status VulkanDevice::SetupLitMesh(const LitMeshShaders& shaders) {
    vkDeviceWaitIdle(device_);

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

    // W20 hot-reload: if lit is already up, only rebuild pipelines (keep meshes/textures).
    if (lit_ready_ && lit_pipeline_layout_ != VK_NULL_HANDLE && render_pass_ != VK_NULL_HANDLE) {
        if (auto st = CreateLitPipeline(vs.value(), ps.value()); !st) {
            return st;
        }
        LogInfo("Vulkan lit PSO hot-reload Ok (geometry retained)");
        return Status::Ok();
    }

    DestroyLitResources();

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

Status VulkanDevice::SetFrameLighting(const FrameLighting& lighting) {
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
    for (int i = 0; i < 32; ++i) {
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
    data.z_slices = 4.f;
    data.z_near = 0.5f;
    data.z_far = 80.f;
    for (int i = 0; i < 128; ++i) {
        data.tile_light_count[i] =
                static_cast<float>(lighting_.tile_light_count[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < 1024; ++i) {
        data.tile_light_index[i] =
                static_cast<float>(lighting_.tile_light_index[static_cast<std::size_t>(i)]);
    }
    data.enable_probe_gi = lighting_.enable_probe_gi ? 1.f : 0.f;
    data.probe_gi_intensity = lighting_.probe_gi_intensity;
    data.probe_rgb_scale = lighting_.probe_rgb_scale;
    data.probe_nx = static_cast<float>(lighting_.probe_nx);
    data.probe_origin[0] = lighting_.probe_origin.x;
    data.probe_origin[1] = lighting_.probe_origin.y;
    data.probe_origin[2] = lighting_.probe_origin.z;
    data.probe_ny = static_cast<float>(lighting_.probe_ny);
    data.probe_spacing[0] = lighting_.probe_spacing.x;
    data.probe_spacing[1] = lighting_.probe_spacing.y;
    data.probe_spacing[2] = lighting_.probe_spacing.z;
    data.probe_nz = static_cast<float>(lighting_.probe_nz);
    data.enable_soft_shadow_mask = lighting_.enable_soft_shadow_mask ? 1.f : 0.f;

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

Status VulkanDevice::UploadInstanceTransforms(std::span<const Mat4> worlds) {
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

Status VulkanDevice::DrawLitInstanced(const LitDrawItem& prototype, std::uint32_t instance_count) {
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

Status VulkanDevice::DrawLitCube(const LitDrawItem& item) {
    return DrawLitCubes(std::span<const LitDrawItem>(&item, 1));
}

Status VulkanDevice::DrawTransparentLitCubes(std::span<const LitDrawItem> items) {
    return DrawLitCubesWithPipeline(items, lit_pipeline_transparent_);
}

Status VulkanDevice::DrawLitCubes(std::span<const LitDrawItem> items) {
    return DrawLitCubesWithPipeline(items, lit_pipeline_);
}

Status VulkanDevice::DrawLitCubesWithPipeline(std::span<const LitDrawItem> items, VkPipeline pipeline) {
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
        od.pad = BindlessAlbedoHeapPad(od.tex_slot);

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

Status VulkanDevice::UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height,
                                                     int slot) {
    if (!lit_ready_) {
        return Status::Fail("SetupLitMesh not called");
    }
    if (slot < 0 || slot > 1) {
        return Status::Fail("Invalid albedo slot");
    }
    const std::uint32_t binding = (slot == 0) ? 4u : 6u;
    return UploadRgba2D(lit_albedo_[slot], rgba, width, height, binding, lit_linear_sampler_);
}

Status VulkanDevice::UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height, int slot) {
    if (!lit_ready_) {
        return Status::Fail("SetupLitMesh not called");
    }
    if (slot < 0 || slot > 1) {
        return Status::Fail("Invalid ORM slot");
    }
    const std::uint32_t binding = (slot == 0) ? 5u : 7u;
    return UploadRgba2D(lit_orm_[slot], rgba, width, height, binding, lit_linear_sampler_);
}

Status VulkanDevice::UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                                                 std::span<const std::uint32_t> indices) {
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
        WaitGpuSubmitted();
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

void VulkanDevice::DestroyMeshSlot(MeshSlotGpu& slot) {
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

void VulkanDevice::UpdateLitCombinedBinding(std::uint32_t binding, VkImageView view, VkSampler sampler) {
    UpdateLitCombinedBinding(binding, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanDevice::UpdateLitInstanceBinding(VkBuffer buffer, VkDeviceSize range) {
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

void VulkanDevice::UpdateLitCombinedBinding(std::uint32_t binding, VkImageView view, VkSampler sampler,
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

Status VulkanDevice::BeginLitRenderPass(const ColorRgba& color, bool load_contents) {
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

Status VulkanDevice::CreateFrameSync() {
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

Status VulkanDevice::CreateLitRenderPass() {
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

Status VulkanDevice::CreateLitPipeline(const std::vector<std::uint8_t>& vs_spv,
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

    // Lit set: b0/b1 UBO, combined t0..t8 → bindings 2..10, SSBO t9→11, probe t10→12,
    // GI atlas t11→13, soft-shadow mask t12→14.
    VkDescriptorSetLayoutBinding binds[15]{};
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
    // W20: probe GI atlas t11→13, soft-shadow mask t12→14.
    binds[13].binding = 13;
    binds[13].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[13].descriptorCount = 1;
    binds[13].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[14].binding = 14;
    binds[14].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[14].descriptorCount = 1;
    binds[14].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 15;
    dsl.pBindings = binds;
    if (lit_set_layout_ == VK_NULL_HANDLE) {
        if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr, &lit_set_layout_) != VK_SUCCESS) {
            vkDestroyShaderModule(device_, vs, nullptr);
            vkDestroyShaderModule(device_, ps, nullptr);
            return Status::Fail("vkCreateDescriptorSetLayout failed");
        }
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &lit_set_layout_;
    if (lit_pipeline_layout_ == VK_NULL_HANDLE) {
        if (vkCreatePipelineLayout(device_, &plci, nullptr, &lit_pipeline_layout_) != VK_SUCCESS) {
            vkDestroyShaderModule(device_, vs, nullptr);
            vkDestroyShaderModule(device_, ps, nullptr);
            return Status::Fail("vkCreatePipelineLayout failed");
        }
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

    VkPipeline new_lit = VK_NULL_HANDLE;
    const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                                                                             &new_lit);
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
    VkPipeline new_tr = VK_NULL_HANDLE;
    const VkResult rt = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                                                                                &new_tr);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, ps, nullptr);
    if (rt != VK_SUCCESS) {
        vkDestroyPipeline(device_, new_lit, nullptr);
        return Status::Fail("vkCreateGraphicsPipelines (transparent) failed: " + VkErr(rt));
    }
    // W20: swap on success so a failed create keeps the previous lit PSO.
    if (lit_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, lit_pipeline_, nullptr);
    }
    if (lit_pipeline_transparent_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, lit_pipeline_transparent_, nullptr);
    }
    lit_pipeline_ = new_lit;
    lit_pipeline_transparent_ = new_tr;
    return Status::Ok();
}

Status VulkanDevice::CreateCubeMesh() {
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

void VulkanDevice::DestroyLitResources() {
    lit_ready_ = false;
    shadow_pass_active_ = false;
    bound_cascade_ = -1;
    DestroyCullCompute();
    DestroyTileCullCompute();
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
    DestroyTex2D(probe_gi_atlas_);
    DestroyTex2D(soft_shadow_mask_);
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

}  // namespace engine::rhi
