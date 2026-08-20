#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::SetupPostMesh(const PostShaders& shaders) {
    WaitGpu();

    auto vs = ReadFileBytes(shaders.vs_dxil);
    if (!vs) {
        return vs.status();
    }
    auto ps = ReadFileBytes(shaders.ps_dxil);
    if (!ps) {
        return ps.status();
    }

    D3D12_DESCRIPTOR_RANGE srv_range{};
    srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_range.NumDescriptors = 3;
    srv_range.BaseShaderRegister = 0;
    srv_range.RegisterSpace = 0;
    srv_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srv_range;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].MinLOD = 0.f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].MinLOD = 0.f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2;
    rs.pParameters = params;
    rs.NumStaticSamplers = 2;
    rs.pStaticSamplers = samplers;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
        return Status::Fail(std::string("Post root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                        IID_PPV_ARGS(&post_root_));
    if (FAILED(hr)) {
        return Status::Fail("Create post root signature failed: " + HrToString(hr));
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = post_root_.Get();
    pso.VS = {vs->data(), vs->size()};
    pso.PS = {ps->data(), ps->size()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.InputLayout = {nullptr, 0};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = 1;
    hr = device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&post_pso_));
    if (FAILED(hr)) {
        return Status::Fail("Create post PSO failed: " + HrToString(hr));
    }

    post_cb_.Reset();
    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = kPostCbBytes * kFrameCount;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf,
                                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                IID_PPV_ARGS(&post_cb_));
    if (FAILED(hr)) {
        return Status::Fail("Create post CB failed: " + HrToString(hr));
    }

    post_srv_heap_.Reset();
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap{};
    srv_heap.NumDescriptors = 3;
    srv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srv_heap, IID_PPV_ARGS(&post_srv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("Create post SRV heap failed: " + HrToString(hr));
    }
    if (cbv_srv_uav_descriptor_size_ == 0) {
        cbv_srv_uav_descriptor_size_ =
                device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (!dsv_) {
        if (auto st = CreateDepthBuffer(); !st) {
            return st;
        }
    }
    if (auto st = CreatePostColorTargets(); !st) {
        return st;
    }
    UpdatePostSrvs();

    post_ready_ = true;
    LogInfo("Post SSAO/TAA path ready");
    return Status::Ok();
}

Status D3D12Device::ResolvePostEffects(const PostResolveDesc& desc) {
    if (!post_ready_) {
        return Status::Fail("SetupPostMesh not called");
    }
    if (!scene_color_ || !history_ || !dsv_ || !post_pso_ || !post_cb_ || !post_srv_heap_) {
        return Status::Fail("Post resources missing");
    }

    const auto bb_index = CurrentBbIndex();
    auto* backbuffer = backbuffers_[bb_index].Get();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
                                                                                static_cast<SIZE_T>(bb_index) * rtv_descriptor_size_};

    // Lit already wrote HDR into scene_color_. Sample it directly (no LDR backbuffer copy).
    if (scene_color_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(scene_color_.Get(), scene_color_state_,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        scene_color_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Depth + history readable
    if (depth_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(dsv_.Get(), depth_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        depth_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (history_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        Transition(history_.Get(), history_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        history_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // PostCB (must match post_ssao_taa.hlsl packing)
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
        // W23
        float enable_gtao;
        float enable_fxaa;
        float enable_color_grading;
        float color_grading_strength;
        float fog_box_min[3];
        float enable_fog_box;
        float fog_box_max[3];
        float ssr_roughness_fade;
    } cb{};
    static_assert(sizeof(PostCB) <= 768, "post CB exceeds upload buffer");
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
    cb.enable_gtao = desc.enable_gtao ? 1.f : 0.f;
    cb.enable_fxaa = desc.enable_fxaa ? 1.f : 0.f;
    cb.enable_color_grading = desc.enable_color_grading ? 1.f : 0.f;
    cb.color_grading_strength = desc.color_grading_strength;
    cb.enable_fog_box = desc.enable_fog_box ? 1.f : 0.f;
    cb.fog_box_min[0] = desc.fog_box_min.x;
    cb.fog_box_min[1] = desc.fog_box_min.y;
    cb.fog_box_min[2] = desc.fog_box_min.z;
    cb.fog_box_max[0] = desc.fog_box_max.x;
    cb.fog_box_max[1] = desc.fog_box_max.y;
    cb.fog_box_max[2] = desc.fog_box_max.z;
    cb.ssr_roughness_fade = desc.ssr_roughness_fade;

    void* mapped = nullptr;
    if (FAILED(post_cb_->Map(0, nullptr, &mapped))) {
        return Status::Fail("Map post CB failed");
    }
    std::memcpy(static_cast<char*>(mapped) + PostCbOffset(), &cb, sizeof(cb));
    post_cb_->Unmap(0, nullptr);

    // Fullscreen triangle → LDR backbuffer
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &vp);
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    command_list_->RSSetScissorRects(1, &scissor);

    command_list_->SetPipelineState(post_pso_.Get());
    command_list_->SetGraphicsRootSignature(post_root_.Get());
    ID3D12DescriptorHeap* heaps[] = {post_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootConstantBufferView(
            0, post_cb_->GetGPUVirtualAddress() + PostCbOffset());
    command_list_->SetGraphicsRootDescriptorTable(
            1, post_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    // Copy resolved backbuffer → history_
    Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (history_state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
        Transition(history_.Get(), history_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        history_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    command_list_->CopyResource(history_.Get(), backbuffer);
    Transition(history_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    history_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Transition(backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // Restore depth write + LDR RT for debug/UI
    if (depth_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        Transition(dsv_.Get(), depth_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    post_resolved_this_frame_ = true;
    return Status::Ok();
}

UINT64 D3D12Device::PostCbOffset() const { return static_cast<UINT64>(frame_index_) * kPostCbBytes; }

Status D3D12Device::CreatePostColorTargets() {
    scene_color_.Reset();
    history_.Reset();
    hdr_rtv_heap_.Reset();

    auto make_rt = [&](ComPtr<ID3D12Resource>& out, D3D12_RESOURCE_STATES& state,
                                         DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) -> Status {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width_;
        desc.Height = height_;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = flags;

        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = format;
        clear.Color[0] = 0.14f;
        clear.Color[1] = 0.16f;
        clear.Color[2] = 0.20f;
        clear.Color[3] = 1.f;
        const D3D12_CLEAR_VALUE* clear_ptr =
                (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ? &clear : nullptr;
        const HRESULT hr = device_->CreateCommittedResource(
                &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ? D3D12_RESOURCE_STATE_RENDER_TARGET
                                                                                                                    : D3D12_RESOURCE_STATE_COPY_DEST,
                clear_ptr, IID_PPV_ARGS(&out));
        if (FAILED(hr)) {
            return Status::Fail("Create post color target failed: " + HrToString(hr));
        }
        state = (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
                                ? D3D12_RESOURCE_STATE_RENDER_TARGET
                                : D3D12_RESOURCE_STATE_COPY_DEST;
        return Status::Ok();
    };

    // HDR scene color is the lit/transparent render target.
    if (auto st = make_rt(scene_color_, scene_color_state_, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            !st) {
        return st;
    }
    // History stays LDR (copy of tonemapped backbuffer).
    if (auto st = make_rt(history_, history_state_, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                D3D12_RESOURCE_FLAG_NONE);
            !st) {
        return st;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    const HRESULT hr = device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&hdr_rtv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("Create HDR RTV heap failed");
    }
    device_->CreateRenderTargetView(scene_color_.Get(), nullptr,
                                                                    hdr_rtv_heap_->GetCPUDescriptorHandleForHeapStart());
    return Status::Ok();
}

void D3D12Device::UpdatePostSrvs() {
    if (!post_srv_heap_) {
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE handle = post_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    const UINT incr = cbv_srv_uav_descriptor_size_
                                                ? cbv_srv_uav_descriptor_size_
                                                : device_->GetDescriptorHandleIncrementSize(
                                                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (scene_color_) {
        device_->CreateShaderResourceView(scene_color_.Get(), nullptr, handle);
    }
    handle.ptr += incr;

    if (dsv_) {
        D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv{};
        depth_srv.Format = DXGI_FORMAT_R32_FLOAT;
        depth_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depth_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depth_srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(dsv_.Get(), &depth_srv, handle);
    }
    handle.ptr += incr;

    if (history_) {
        device_->CreateShaderResourceView(history_.Get(), nullptr, handle);
    }
}

}  // namespace engine::rhi
