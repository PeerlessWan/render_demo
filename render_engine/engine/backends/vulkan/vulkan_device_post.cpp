#include "vulkan_device_internal.h"

namespace engine::rhi {

Status VulkanDevice::SetupPostMesh(const PostShaders& shaders) {
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

Status VulkanDevice::ResolvePostEffects(const PostResolveDesc& desc) {
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

Status VulkanDevice::DrawScreenQuads(std::span<const ScreenQuad> quads) {
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

Status VulkanDevice::DrawDebugLines(std::span<const DebugLineVertex> lines_as_segments) {
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

Status VulkanDevice::SetupUiMesh(const SimpleMeshShaders& shaders) {
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

Status VulkanDevice::UploadUiFontAtlas(const std::uint8_t* rgba, int width, int height) {
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

Status VulkanDevice::DrawUiMesh(std::span<const UiVertex> vertices, std::span<const std::uint16_t> indices,
                                    std::span<const UiDrawCmd> commands) {
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

void VulkanDevice::DestroyUiResources() {
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

void VulkanDevice::DestroyQuadResources() {
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

void VulkanDevice::DestroyDebugResources() {
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

Status VulkanDevice::SetupScreenQuads(const std::filesystem::path& vs_path,
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

Status VulkanDevice::SetupDebugLines(const std::filesystem::path& vs_path,
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

Status VulkanDevice::CreatePostColorRenderPass() {
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

void VulkanDevice::DestroyPostFramebuffersOnly() {
    for (VkFramebuffer fb : post_framebuffers_) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
    }
    post_framebuffers_.clear();
}

Status VulkanDevice::CreatePostFramebuffers() {
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

Status VulkanDevice::EnsurePostUb() {
    if (post_ub_ != VK_NULL_HANDLE) {
        return Status::Ok();
    }
    const VkMemoryPropertyFlags host =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    return CreateBuffer(kPostUbSize * kFramesInFlight, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host,
                                            post_ub_, post_ub_mem_);
}

Status VulkanDevice::UploadPostCB(const PostResolveDesc& desc) {
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

Status VulkanDevice::CreatePostPipeline(const std::vector<std::uint8_t>& vs_spv,
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

Status VulkanDevice::EnsurePostDescriptors() {
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

void VulkanDevice::UpdatePostDescriptors() {
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

void VulkanDevice::DestroyPostResources() {
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

}  // namespace engine::rhi
