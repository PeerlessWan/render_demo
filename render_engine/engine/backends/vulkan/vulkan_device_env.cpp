#include "vulkan_device_internal.h"

namespace engine::rhi {

Status VulkanDevice::UploadReflectionCubemap(const std::uint8_t* rgba_faces, int face_size) {
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

Status VulkanDevice::UploadIblIrradianceCubemap(const std::uint8_t* rgba_faces, int face_size) {
    return UploadIblCubemapGpu(rgba_faces, face_size, true);
}

Status VulkanDevice::UploadIblPrefilterCubemap(const std::uint8_t* rgba_faces, int face_size) {
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

Status VulkanDevice::UploadIblBrdfLut(const std::uint8_t* rgba, int w, int h) {
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

Status VulkanDevice::UploadProbeIrradianceAtlas(const float* rgb, int count, int nx, int ny,
                                                                                                int nz) {
    if (!rgb || count <= 0 || nx <= 0 || ny <= 0 || nz <= 0) {
        return Status::Fail(ErrorCode::InvalidArgument, "UploadProbeIrradianceAtlas: invalid args");
    }
    if (count < nx * ny * nz) {
        return Status::Fail(ErrorCode::InvalidArgument, "UploadProbeIrradianceAtlas: count < nx*ny*nz");
    }
    const int w = nx;
    const int h = ny * nz;
    constexpr float kScale = 2.f;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4));
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int pi = x + nx * (y + ny * z);
                const std::size_t src = static_cast<std::size_t>(pi) * 3;
                const int dst_i = x + w * (y + z * ny);
                const std::size_t dst = static_cast<std::size_t>(dst_i) * 4;
                auto pack = [&](float v) -> std::uint8_t {
                    const float t = (std::min)(1.f, (std::max)(0.f, v / kScale));
                    return static_cast<std::uint8_t>(t * 255.f + 0.5f);
                };
                rgba[dst + 0] = pack(rgb[src + 0]);
                rgba[dst + 1] = pack(rgb[src + 1]);
                rgba[dst + 2] = pack(rgb[src + 2]);
                rgba[dst + 3] = 255;
            }
        }
    }
    return UploadRgba2D(probe_gi_atlas_, rgba.data(), w, h, 13, lit_linear_sampler_);
}

Status VulkanDevice::UploadSoftShadowMask(const float* factors, int width, int height) {
    if (!factors || width <= 0 || height <= 0) {
        return Status::Fail(ErrorCode::InvalidArgument, "UploadSoftShadowMask: invalid args");
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4));
    for (int i = 0; i < width * height; ++i) {
        const float t = (std::min)(1.f, (std::max)(0.f, factors[i]));
        const std::uint8_t u = static_cast<std::uint8_t>(t * 255.f + 0.5f);
        const std::size_t dst = static_cast<std::size_t>(i) * 4;
        rgba[dst + 0] = u;
        rgba[dst + 1] = u;
        rgba[dst + 2] = u;
        rgba[dst + 3] = 255;
    }
    return UploadRgba2D(soft_shadow_mask_, rgba.data(), width, height, 14, lit_linear_sampler_);
}

Status VulkanDevice::SetupSkybox(const std::filesystem::path& vs_dxil,
                                     const std::filesystem::path& ps_dxil) {
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

Status VulkanDevice::UploadSkyCubemap(const std::uint8_t* rgba_faces, int face_size) {
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

Status VulkanDevice::DrawSkybox(const Mat4& view_rot_proj) {
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

void VulkanDevice::DestroyIblCube() {
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

void VulkanDevice::DestroyReflectionProbeCube() {
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

void VulkanDevice::DestroySkyCube() {
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

void VulkanDevice::DestroySkyResources() {
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

}  // namespace engine::rhi
