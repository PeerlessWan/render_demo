#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::EnsureProbeFaceTargets(int face_size) {
    face_size = (std::max)(8, face_size);
    if (probe_face_size_ == face_size && probe_face_color_ && probe_face_depth_) {
        return Status::Ok();
    }
    const bool resizing = probe_face_color_ != nullptr;
    if (resizing) {
        WaitGpu();
    }
    probe_face_color_.Reset();
    probe_face_depth_.Reset();
    probe_rtv_heap_.Reset();
    probe_dsv_heap_.Reset();
    probe_face_size_ = face_size;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap{};
    rtv_heap.NumDescriptors = 1;
    rtv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HRESULT hr = device_->CreateDescriptorHeap(&rtv_heap, IID_PPV_ARGS(&probe_rtv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("probe RTV heap failed");
    }
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap{};
    dsv_heap.NumDescriptors = 1;
    dsv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = device_->CreateDescriptorHeap(&dsv_heap, IID_PPV_ARGS(&probe_dsv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("probe DSV heap failed");
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC color{};
    color.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    color.Width = static_cast<UINT64>(face_size);
    color.Height = static_cast<UINT>(face_size);
    color.DepthOrArraySize = 1;
    color.MipLevels = 1;
    color.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    color.SampleDesc.Count = 1;
    color.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear.Color[0] = 0.05f;
    clear.Color[1] = 0.06f;
    clear.Color[2] = 0.08f;
    clear.Color[3] = 1.f;
    hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &color,
                                                                                D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                                                                IID_PPV_ARGS(&probe_face_color_));
    if (FAILED(hr)) {
        return Status::Fail("probe color RT failed");
    }
    device_->CreateRenderTargetView(probe_face_color_.Get(), nullptr,
                                                                    probe_rtv_heap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_RESOURCE_DESC depth = color;
    depth.Format = DXGI_FORMAT_D32_FLOAT;
    depth.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dclear{};
    dclear.Format = DXGI_FORMAT_D32_FLOAT;
    dclear.DepthStencil.Depth = 1.f;
    hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth,
                                                                                D3D12_RESOURCE_STATE_DEPTH_WRITE, &dclear,
                                                                                IID_PPV_ARGS(&probe_face_depth_));
    if (FAILED(hr)) {
        return Status::Fail("probe depth RT failed");
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(probe_face_depth_.Get(), &dsv,
                                                                    probe_dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    return Status::Ok();
}

Mat4 D3D12Device::MakeProbeFaceVp(const Vec3& probe_pos, int face) {
    Vec3 forward{};
    Vec3 up{0.f, -1.f, 0.f};
    switch (face) {
        case 0:
            forward = {1.f, 0.f, 0.f};
            break;
        case 1:
            forward = {-1.f, 0.f, 0.f};
            break;
        case 2:
            forward = {0.f, 1.f, 0.f};
            up = {0.f, 0.f, 1.f};
            break;
        case 3:
            forward = {0.f, -1.f, 0.f};
            up = {0.f, 0.f, -1.f};
            break;
        case 4:
            forward = {0.f, 0.f, 1.f};
            break;
        default:
            forward = {0.f, 0.f, -1.f};
            break;
    }
    return Mat4::Perspective(1.57079632679f, 1.f, 0.05f, 80.f) *
                 Mat4::LookAt(probe_pos, probe_pos + forward, up);
}

Status D3D12Device::CaptureReflectionProbeGpu(const Vec3& probe_pos, int face_size,
                                                                 std::span<const LitDrawItem> items) {
    if (!lit_ready_) {
        return Status::Fail("SetupLitMesh not called");
    }
    if (auto st = EnsureProbeFaceTargets(face_size); !st) {
        return st;
    }
    if (!reflection_cube_ || probe_cube_size_ != probe_face_size_) {
        // Create/replace cubemap without WaitGpu mid-frame (safe if first use this frame).
        reflection_cube_.Reset();
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(probe_face_size_);
        desc.Height = static_cast<UINT>(probe_face_size_);
        desc.DepthOrArraySize = 6;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const HRESULT hr = device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&reflection_cube_));
        if (FAILED(hr)) {
            return Status::Fail("Create probe cubemap failed");
        }
        probe_cube_size_ = probe_face_size_;
        reflection_cube_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    FrameLighting saved = lighting_;
    for (int face = 0; face < 6; ++face) {
        FrameLighting face_lighting = saved;
        face_lighting.view_proj = MakeProbeFaceVp(probe_pos, face);
        face_lighting.prev_view_proj = face_lighting.view_proj;
        face_lighting.eye = probe_pos;
        face_lighting.enable_reflection_probe = false;
        face_lighting.enable_ibl = false;
        face_lighting.enable_taa = false;
        face_lighting.enable_ssao = false;
        if (auto st = SetFrameLighting(face_lighting); !st) {
            return st;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = probe_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = probe_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        const float clear_color[] = {0.05f, 0.06f, 0.08f, 1.f};
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
        command_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
        D3D12_VIEWPORT vp{};
        vp.Width = static_cast<float>(probe_face_size_);
        vp.Height = static_cast<float>(probe_face_size_);
        vp.MaxDepth = 1.f;
        D3D12_RECT sc{0, 0, probe_face_size_, probe_face_size_};
        command_list_->RSSetViewports(1, &vp);
        command_list_->RSSetScissorRects(1, &sc);

        // Draw into probe face without rebinding scene color.
        command_list_->SetPipelineState(lit_pso_.Get());
        command_list_->SetGraphicsRootSignature(lit_root_.Get());
        command_list_->SetGraphicsRootConstantBufferView(
                0, frame_cb_->GetGPUVirtualAddress() + FrameCbOffset());
        ID3D12DescriptorHeap* heaps[] = {shadow_srv_heap_.Get()};
        command_list_->SetDescriptorHeaps(1, heaps);
        command_list_->SetGraphicsRootDescriptorTable(
                2, shadow_srv_heap_->GetGPUDescriptorHandleForHeapStart());
        if (auto* ib = CurrentInstanceBuf()) {
            command_list_->SetGraphicsRootShaderResourceView(3, ib->GetGPUVirtualAddress());
        }
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        struct ObjectData {
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
        for (std::size_t i = 0; i < items.size(); ++i) {
            const int slot = items[i].mesh_slot;
            if (slot < 0 || slot >= kMaxMeshSlots || mesh_slots_[slot].index_count == 0) {
                continue;
            }
            command_list_->IASetVertexBuffers(0, 1, &mesh_slots_[slot].vbv);
            command_list_->IASetIndexBuffer(&mesh_slots_[slot].ibv);
            ObjectData od{};
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
            od.pad = -1.f;  // probe faces: classic only (no bindless_hot_path)
            const auto offset = ObjectCbOffset(i);
            void* ptr = nullptr;
            if (FAILED(object_cb_->Map(0, nullptr, &ptr))) {
                return Status::Fail("Map object CB failed");
            }
            std::memcpy(static_cast<char*>(ptr) + offset, &od, sizeof(od));
            object_cb_->Unmap(0, nullptr);
            command_list_->SetGraphicsRootConstantBufferView(
                    1, object_cb_->GetGPUVirtualAddress() + offset);
            command_list_->DrawIndexedInstanced(mesh_slots_[slot].index_count, 1, 0, 0, 0);
        }

        Transition(probe_face_color_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (reflection_cube_state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            Transition(reflection_cube_.Get(), reflection_cube_state_,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
            reflection_cube_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = probe_face_color_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = reflection_cube_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = static_cast<UINT>(face);
        command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        Transition(probe_face_color_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                             D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    if (reflection_cube_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(reflection_cube_.Get(), reflection_cube_state_,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        reflection_cube_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (auto st = BindCubeSrv(10, reflection_cube_.Get()); !st) {
        return st;
    }
    if (auto st = SetFrameLighting(saved); !st) {
        return st;
    }
    BindSceneColorTargets();
    return Status::Ok();
}

UINT64 D3D12Device::SkyCbOffset() const { return static_cast<UINT64>(frame_index_) * 256ull; }

Status D3D12Device::BindReflectionCubeSrv() {
    return BindCubeSrv(10, reflection_cube_.Get());
}

Status D3D12Device::EnsureDefaultReflectionCubemap() {
    constexpr int kFace = 8;
    std::vector<std::uint8_t> faces(static_cast<std::size_t>(6 * kFace * kFace * 4));
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < kFace; ++y) {
            for (int x = 0; x < kFace; ++x) {
                const std::size_t i =
                        static_cast<std::size_t>(((face * kFace + y) * kFace + x) * 4);
                const float t = static_cast<float>(y) / static_cast<float>(kFace);
                faces[i + 0] = static_cast<std::uint8_t>(40 + 80 * t);
                faces[i + 1] = static_cast<std::uint8_t>(50 + 90 * t);
                faces[i + 2] = static_cast<std::uint8_t>(70 + 110 * t);
                faces[i + 3] = 255;
            }
        }
    }
    if (auto st = UploadReflectionCubemap(faces.data(), kFace); !st) {
        return st;
    }
    if (auto st = UploadIblPrefilterCubemap(faces.data(), kFace); !st) {
        return st;
    }
    if (auto st = UploadIblIrradianceCubemap(faces.data(), kFace); !st) {
        return st;
    }
    std::vector<std::uint8_t> lut(128 * 128 * 4);
    for (std::size_t i = 0; i < lut.size(); i += 4) {
        lut[i + 0] = 200;
        lut[i + 1] = 40;
        lut[i + 2] = 0;
        lut[i + 3] = 255;
    }
    return UploadIblBrdfLut(lut.data(), 128, 128);
}

Status D3D12Device::UploadReflectionCubemap(const std::uint8_t* rgba_faces, int face_size) {
    if (auto st = UploadCubemapResource(reflection_cube_, rgba_faces, face_size); !st) {
        return st;
    }
    reflection_cube_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    probe_cube_size_ = face_size;
    return BindCubeSrv(10, reflection_cube_.Get());
}

Status D3D12Device::BindCubeSrv(UINT slot, ID3D12Resource* cube) {
    if (!shadow_srv_heap_ || !cube) {
        return Status::Fail("Cube SRV bind missing");
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.TextureCube.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(slot) * cbv_srv_uav_descriptor_size_;
    device_->CreateShaderResourceView(cube, &srv, handle);
    return Status::Ok();
}

Status D3D12Device::UploadIblIrradianceCubemap(const std::uint8_t* rgba_faces, int face_size) {
    // Dedicated irradiance cube at t7.
    if (auto st = UploadCubemapResource(ibl_irradiance_, rgba_faces, face_size); !st) {
        return st;
    }
    return BindCubeSrv(7, ibl_irradiance_.Get());
}

Status D3D12Device::UploadIblPrefilterCubemap(const std::uint8_t* rgba_faces, int face_size) {
    // Dedicated specular prefilter at t6 (independent of reflection probe t10).
    if (auto st = UploadCubemapResource(ibl_prefilter_, rgba_faces, face_size); !st) {
        return st;
    }
    return BindCubeSrv(6, ibl_prefilter_.Get());
}

Status D3D12Device::UploadIblBrdfLut(const std::uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) {
        return Status::Fail("Invalid BRDF LUT");
    }
    return UploadRgbaTexture(ibl_brdf_lut_, 8, rgba, w, h);
}

Status D3D12Device::EnsureDefaultProbeGiAndSoftShadowTextures() {
    const std::uint8_t black[4] = {0, 0, 0, 255};
    const std::uint8_t white[4] = {255, 255, 255, 255};
    if (!probe_gi_atlas_) {
        if (auto st = UploadRgbaTexture(probe_gi_atlas_, 11, black, 1, 1); !st) {
            return st;
        }
    }
    if (!soft_shadow_mask_) {
        if (auto st = UploadRgbaTexture(soft_shadow_mask_, 12, white, 1, 1); !st) {
            return st;
        }
    }
    return Status::Ok();
}

Status D3D12Device::UploadProbeIrradianceAtlas(const float* rgb, int count, int nx, int ny,
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
    return UploadRgbaTexture(probe_gi_atlas_, 11, rgba.data(), w, h);
}

Status D3D12Device::UploadSoftShadowMask(const float* factors, int width, int height) {
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
    return UploadRgbaTexture(soft_shadow_mask_, 12, rgba.data(), width, height);
}

Status D3D12Device::SetupSkybox(const std::filesystem::path& vs_dxil,
                                     const std::filesystem::path& ps_dxil) {
    if (!device_ || vs_dxil.empty() || ps_dxil.empty()) {
        return Status::Fail("SetupSkybox: invalid");
    }
    std::ifstream vs_in(vs_dxil, std::ios::binary);
    std::ifstream ps_in(ps_dxil, std::ios::binary);
    if (!vs_in || !ps_in) {
        return Status::Fail("Skybox shaders missing");
    }
    std::vector<char> vs((std::istreambuf_iterator<char>(vs_in)), std::istreambuf_iterator<char>());
    std::vector<char> ps((std::istreambuf_iterator<char>(ps_in)), std::istreambuf_iterator<char>());
    if (vs.empty() || ps.empty()) {
        return Status::Fail("Skybox shaders empty");
    }

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2;
    rs.pParameters = params;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &samp;
    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
        return Status::Fail("Serialize sky root failed");
    }
    if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                                    IID_PPV_ARGS(&sky_root_)))) {
        return Status::Fail("Create sky root failed");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = sky_root_.Get();
    pso.VS = {vs.data(), vs.size()};
    pso.PS = {ps.data(), ps.size()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&sky_pso_)))) {
        // Fallback LDR swapchain format if HDR RT not used in this build path.
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (FAILED(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&sky_pso_)))) {
            return Status::Fail("Create sky PSO failed");
        }
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&sky_srv_heap_)))) {
        return Status::Fail("Create sky SRV heap failed");
    }

    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC cb{};
    cb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cb.Width = 256ull * kFrameCount;
    cb.Height = 1;
    cb.DepthOrArraySize = 1;
    cb.MipLevels = 1;
    cb.SampleDesc.Count = 1;
    cb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &cb,
                                                                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                            IID_PPV_ARGS(&sky_cb_)))) {
        return Status::Fail("Create sky CB failed");
    }
    sky_ready_ = true;
    return Status::Ok();
}

Status D3D12Device::UploadSkyCubemap(const std::uint8_t* rgba_faces, int face_size) {
    if (!sky_ready_) {
        return Status::Fail("SetupSkybox first");
    }
    if (auto st = UploadCubemapResource(sky_cube_, rgba_faces, face_size); !st) {
        return st;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.TextureCube.MipLevels = 1;
    device_->CreateShaderResourceView(sky_cube_.Get(), &srv,
                                                                        sky_srv_heap_->GetCPUDescriptorHandleForHeapStart());
    sky_uploaded_ = true;
    engine::SetFeatureOverride("skybox", true);
    return Status::Ok();
}

Status D3D12Device::DrawSkybox(const Mat4& view_rot_proj) {
    if (!sky_ready_ || !sky_uploaded_ || !command_list_) {
        return Status::Ok();
    }
    BindSceneColorTargets();
    void* mapped = nullptr;
    if (FAILED(sky_cb_->Map(0, nullptr, &mapped)) || !mapped) {
        return Status::Fail("Map sky CB failed");
    }
    const UINT64 off = SkyCbOffset();
    std::memcpy(static_cast<char*>(mapped) + off, view_rot_proj.m.data(), sizeof(float) * 16);
    sky_cb_->Unmap(0, nullptr);

    command_list_->SetPipelineState(sky_pso_.Get());
    command_list_->SetGraphicsRootSignature(sky_root_.Get());
    ID3D12DescriptorHeap* heaps[] = {sky_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootConstantBufferView(0, sky_cb_->GetGPUVirtualAddress() + off);
    command_list_->SetGraphicsRootDescriptorTable(
            1, sky_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(36, 1, 0, 0);
    return Status::Ok();
}

Status D3D12Device::UploadCubemapResource(ComPtr<ID3D12Resource>& cube, const std::uint8_t* rgba_faces,
                                                         int face_size) {
    if (!rgba_faces || face_size <= 0 || !device_) {
        return Status::Fail("Invalid cubemap upload");
    }
    WaitGpu();
    cube.Reset();
    const UINT size = static_cast<UINT>(face_size);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = 6;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                                                IID_PPV_ARGS(&cube));
    if (FAILED(hr)) {
        return Status::Fail("Create cubemap failed: " + HrToString(hr));
    }
    UINT64 total_bytes = 0;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 6> layouts{};
    std::array<UINT, 6> num_rows{};
    std::array<UINT64, 6> row_sizes{};
    device_->GetCopyableFootprints(&desc, 0, 6, 0, layouts.data(), num_rows.data(),
                                                                 row_sizes.data(), &total_bytes);
    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = total_bytes;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    hr = device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buf,
                                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                IID_PPV_ARGS(&upload));
    if (FAILED(hr)) {
        return Status::Fail("Create cubemap upload failed");
    }
    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped)) || !mapped) {
        return Status::Fail("Map cubemap upload failed");
    }
    auto* dst = static_cast<std::uint8_t*>(mapped);
    for (UINT face = 0; face < 6; ++face) {
        const auto* src_face = rgba_faces + static_cast<std::size_t>(face) * size * size * 4;
        for (UINT row = 0; row < size; ++row) {
            std::memcpy(dst + layouts[face].Offset + row * layouts[face].Footprint.RowPitch,
                                    src_face + row * size * 4, size * 4);
        }
    }
    upload->Unmap(0, nullptr);
    const auto frame = frame_index_;
    if (FAILED(allocators_[frame]->Reset()) ||
            FAILED(command_list_->Reset(allocators_[frame].Get(), nullptr))) {
        return Status::Fail("Cubemap command list reset failed");
    }
    for (UINT face = 0; face < 6; ++face) {
        D3D12_TEXTURE_COPY_LOCATION dst_loc{};
        dst_loc.pResource = cube.Get();
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = face;
        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource = upload.Get();
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint = layouts[face];
        command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
    }
    Transition(cube.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (FAILED(command_list_->Close())) {
        return Status::Fail("Cubemap Close failed");
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitGpu();
    return Status::Ok();
}

}  // namespace engine::rhi
