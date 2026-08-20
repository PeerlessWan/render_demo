#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::DrawScreenQuads(std::span<const ScreenQuad> quads) {
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

    const UINT bytes = static_cast<UINT>(verts.size() * sizeof(QuadVertex));
    auto& quad_vb = quad_vb_[frame_index_];
    if (!quad_vb) {
        return Status::Fail("Screen quad VB not preallocated");
    }
    const UINT draw_bytes = (std::min)(bytes, quad_vb_capacity_);
    const UINT draw_verts =
            draw_bytes / static_cast<UINT>(sizeof(QuadVertex));
    const UINT draw_verts_aligned = draw_verts - (draw_verts % 6);  // keep whole quads
    if (draw_verts_aligned == 0) {
        return Status::Ok();
    }

    void* mapped = nullptr;
    if (FAILED(quad_vb->Map(0, nullptr, &mapped))) {
        return Status::Fail("Map quad VB failed");
    }
    std::memcpy(mapped, verts.data(), draw_verts_aligned * sizeof(QuadVertex));
    quad_vb->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = quad_vb->GetGPUVirtualAddress();
    vbv.SizeInBytes = draw_verts_aligned * sizeof(QuadVertex);
    vbv.StrideInBytes = sizeof(QuadVertex);

    command_list_->SetPipelineState(quad_pso_.Get());
    command_list_->SetGraphicsRootSignature(quad_root_.Get());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->IASetVertexBuffers(0, 1, &vbv);
    command_list_->DrawInstanced(draw_verts_aligned, 1, 0, 0);
    screen_quad_draws_ += draw_verts_aligned / 6;
    return Status::Ok();
}

Status D3D12Device::DrawDebugLines(std::span<const DebugLineVertex> lines_as_segments) {
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

    const UINT bytes =
            static_cast<UINT>(lines_as_segments.size() * sizeof(DebugLineVertex));
    auto& debug_vb = debug_vb_[frame_index_];
    // Never grow/destroy mid-frame — clip to preallocated capacity (BeginFrame waited this slot).
    if (!debug_vb || debug_vb_capacity_ == 0) {
        return Status::Fail("Debug VB not preallocated");
    }
    const UINT draw_bytes = (std::min)(bytes, debug_vb_capacity_);
    const UINT draw_verts =
            draw_bytes / static_cast<UINT>(sizeof(DebugLineVertex));
    const UINT draw_verts_aligned = draw_verts - (draw_verts % 2);
    if (draw_verts_aligned == 0) {
        return Status::Ok();
    }

    void* mapped = nullptr;
    if (FAILED(debug_vb->Map(0, nullptr, &mapped))) {
        return Status::Fail("Map debug VB failed");
    }
    std::memcpy(mapped, lines_as_segments.data(),
                            draw_verts_aligned * sizeof(DebugLineVertex));
    debug_vb->Unmap(0, nullptr);

    float vp[16]{};
    std::memcpy(vp, lighting_.view_proj.m.data(), sizeof(vp));
    void* cb_ptr = nullptr;
    if (FAILED(debug_cb_->Map(0, nullptr, &cb_ptr))) {
        return Status::Fail("Map debug CB failed");
    }
    const UINT64 cb_off = static_cast<UINT64>(frame_index_) * 256ull;
    std::memcpy(static_cast<char*>(cb_ptr) + cb_off, vp, sizeof(vp));
    debug_cb_->Unmap(0, nullptr);

    // Ensure color+depth targets (post restores them; BeginFrame also binds).
    const auto index = CurrentBbIndex();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
                                                                                static_cast<SIZE_T>(index) * rtv_descriptor_size_};
    if (dsv_ && depth_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        Transition(dsv_.Get(), depth_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
            dsv_ ? dsv_heap_->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, dsv_ ? &dsv : nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &viewport);
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    command_list_->RSSetScissorRects(1, &scissor);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = debug_vb->GetGPUVirtualAddress();
    vbv.SizeInBytes = draw_verts_aligned * sizeof(DebugLineVertex);
    vbv.StrideInBytes = sizeof(DebugLineVertex);

    command_list_->SetPipelineState(debug_pso_.Get());
    command_list_->SetGraphicsRootSignature(debug_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
            0, debug_cb_->GetGPUVirtualAddress() + cb_off);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    command_list_->IASetVertexBuffers(0, 1, &vbv);
    command_list_->DrawInstanced(draw_verts_aligned, 1, 0, 0);
    return Status::Ok();
}

Status D3D12Device::SetupUiMesh(const SimpleMeshShaders& shaders) {
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
    srv_range.NumDescriptors = 1;
    srv_range.BaseShaderRegister = 0;
    srv_range.RegisterSpace = 0;
    srv_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER ui_params[2]{};
    ui_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    ui_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    ui_params[0].Descriptor.ShaderRegister = 0;
    ui_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    ui_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    ui_params[1].DescriptorTable.NumDescriptorRanges = 1;
    ui_params[1].DescriptorTable.pDescriptorRanges = &srv_range;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.MinLOD = 0.f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2;
    rs.pParameters = ui_params;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &sampler;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
        return Status::Fail(std::string("UI root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                        IID_PPV_ARGS(&ui_root_));
    if (FAILED(hr)) {
        return Status::Fail("Create UI root signature failed: " + HrToString(hr));
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = ui_root_.Get();
    pso.VS = {vs->data(), vs->size()};
    pso.PS = {ps->data(), ps->size()};
    pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.InputLayout = {layout, 3};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    hr = device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&ui_pso_));
    if (FAILED(hr)) {
        return Status::Fail("Create UI PSO failed: " + HrToString(hr));
    }

    ui_cb_.Reset();
    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = 256ull * kFrameCount;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf,
                                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                IID_PPV_ARGS(&ui_cb_));
    if (FAILED(hr)) {
        return Status::Fail("Create UI CB failed: " + HrToString(hr));
    }

    // Preallocate ImGui mesh per in-flight frame so Map never races GPU.
    constexpr UINT kUiVerts = 16384;
    constexpr UINT kUiIdx = 49152;
    auto create_ui_buf = [&](UINT bytes, ComPtr<ID3D12Resource>& out) -> Status {
        out.Reset();
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = bytes;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT chr = device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&out));
        if (FAILED(chr)) {
            return Status::Fail("Create UI mesh buffer failed: " + HrToString(chr));
        }
        return Status::Ok();
    };
    for (std::uint32_t fi = 0; fi < kFrameCount; ++fi) {
        if (auto st = create_ui_buf(kUiVerts * static_cast<UINT>(sizeof(UiVertex)), ui_vb_[fi]);
                !st) {
            return st;
        }
        if (auto st = create_ui_buf(kUiIdx * static_cast<UINT>(sizeof(std::uint16_t)), ui_ib_[fi]);
                !st) {
            return st;
        }
    }
    ui_vb_capacity_ = kUiVerts * static_cast<UINT>(sizeof(UiVertex));
    ui_ib_capacity_ = kUiIdx * static_cast<UINT>(sizeof(std::uint16_t));

    ui_ready_ = true;
    return Status::Ok();
}

Status D3D12Device::UploadUiFontAtlas(const std::uint8_t* rgba, int width, int height) {
    if (!ui_ready_) {
        return Status::Fail("SetupUiMesh not called");
    }
    if (!rgba || width <= 0 || height <= 0) {
        return Status::Fail(ErrorCode::InvalidArgument, "Invalid font atlas size");
    }

    ui_font_.Reset();
    ui_font_upload_.Reset();
    ui_srv_heap_.Reset();
    ui_font_uploaded_ = false;

    const UINT w = static_cast<UINT>(width);
    const UINT h = static_cast<UINT>(height);

    D3D12_RESOURCE_DESC tex_desc{};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Width = w;
    tex_desc.Height = h;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                                                                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                                                IID_PPV_ARGS(&ui_font_));
    if (FAILED(hr)) {
        return Status::Fail("Create UI font texture failed: " + HrToString(hr));
    }

    UINT64 upload_size = 0;
    device_->GetCopyableFootprints(&tex_desc, 0, 1, 0, nullptr, nullptr, nullptr, &upload_size);
    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC upload_desc{};
    upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_desc.Width = upload_size;
    upload_desc.Height = 1;
    upload_desc.DepthOrArraySize = 1;
    upload_desc.MipLevels = 1;
    upload_desc.SampleDesc.Count = 1;
    upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> upload;
    hr = device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                IID_PPV_ARGS(&upload));
    if (FAILED(hr)) {
        return Status::Fail("Create UI font upload failed: " + HrToString(hr));
    }

    WaitGpu();
    allocators_[0]->Reset();
    command_list_->Reset(allocators_[0].Get(), nullptr);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT num_rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    device_->GetCopyableFootprints(&tex_desc, 0, 1, 0, &layout, &num_rows, &row_size, &total);
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE no_read{0, 0};
    upload->Map(0, &no_read, reinterpret_cast<void**>(&mapped));
    auto* dst = mapped + layout.Offset;
    for (UINT y = 0; y < h; ++y) {
        std::memcpy(dst + y * layout.Footprint.RowPitch, rgba + y * w * 4, w * 4);
    }
    upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource = ui_font_.Get();
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource = upload.Get();
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = layout;
    command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

    Transition(ui_font_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list_->Close();
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitGpu();

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap{};
    srv_heap.NumDescriptors = 1;
    srv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srv_heap, IID_PPV_ARGS(&ui_srv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("Create UI SRV heap failed: " + HrToString(hr));
    }
    device_->CreateShaderResourceView(ui_font_.Get(), nullptr,
                                                                        ui_srv_heap_->GetCPUDescriptorHandleForHeapStart());

    ui_font_upload_ = upload;
    ui_font_uploaded_ = true;
    return Status::Ok();
}

Status D3D12Device::DrawUiMesh(std::span<const UiVertex> vertices, std::span<const std::uint16_t> indices,
                                    std::span<const UiDrawCmd> commands) {
    if (commands.empty()) {
        return Status::Ok();
    }
    if (!ui_ready_ || !ui_cb_ || !ui_pso_ || !ui_root_) {
        return Status::Fail("SetupUiMesh not called");
    }
    if (!ui_font_uploaded_ || !ui_font_ || !ui_srv_heap_) {
        return Status::Fail("UploadUiFontAtlas not called");
    }
    if (vertices.empty() || indices.empty()) {
        return Status::Ok();
    }
    if (width_ == 0 || height_ == 0) {
        return Status::Fail("Invalid viewport size for UI mesh");
    }

    struct UiCBData {
        float inv_display[2];
        float pad[2];
    } cb{};
    cb.inv_display[0] = 1.f / static_cast<float>(width_);
    cb.inv_display[1] = 1.f / static_cast<float>(height_);

    void* mapped = nullptr;
    if (FAILED(ui_cb_->Map(0, nullptr, &mapped))) {
        return Status::Fail("Map UI CB failed");
    }
    const UINT64 ui_cb_off = static_cast<UINT64>(frame_index_) * 256ull;
    std::memcpy(static_cast<char*>(mapped) + ui_cb_off, &cb, sizeof(cb));
    ui_cb_->Unmap(0, nullptr);

    const UINT vb_bytes = static_cast<UINT>(vertices.size() * sizeof(UiVertex));
    const UINT ib_bytes = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));
    auto& ui_vb = ui_vb_[frame_index_];
    auto& ui_ib = ui_ib_[frame_index_];
    // Clip if over preallocated capacity — never Reset/grow mid-frame.
    if (!ui_vb || !ui_ib || ui_vb_capacity_ < vb_bytes || ui_ib_capacity_ < ib_bytes) {
        return Status::Fail("UI mesh exceeds preallocated capacity");
    }

    if (FAILED(ui_vb->Map(0, nullptr, &mapped))) {
        return Status::Fail("Map UI VB failed");
    }
    std::memcpy(mapped, vertices.data(), vb_bytes);
    ui_vb->Unmap(0, nullptr);

    if (FAILED(ui_ib->Map(0, nullptr, &mapped))) {
        return Status::Fail("Map UI IB failed");
    }
    std::memcpy(mapped, indices.data(), ib_bytes);
    ui_ib->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = ui_vb->GetGPUVirtualAddress();
    vbv.SizeInBytes = vb_bytes;
    vbv.StrideInBytes = sizeof(UiVertex);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = ui_ib->GetGPUVirtualAddress();
    ibv.SizeInBytes = ib_bytes;
    ibv.Format = DXGI_FORMAT_R16_UINT;

    command_list_->SetPipelineState(ui_pso_.Get());
    command_list_->SetGraphicsRootSignature(ui_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
            0, ui_cb_->GetGPUVirtualAddress() + ui_cb_off);
    ID3D12DescriptorHeap* heaps[] = {ui_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootDescriptorTable(
            1, ui_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->IASetVertexBuffers(0, 1, &vbv);
    command_list_->IASetIndexBuffer(&ibv);

    D3D12_VIEWPORT ui_vp{};
    ui_vp.Width = static_cast<float>(width_);
    ui_vp.Height = static_cast<float>(height_);
    ui_vp.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &ui_vp);

    const D3D12_RECT full_scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    const float vp_w = static_cast<float>(width_);
    const float vp_h = static_cast<float>(height_);

    for (const auto& cmd : commands) {
        D3D12_RECT scissor{};
        scissor.left = static_cast<LONG>((std::max)(0.f, cmd.clip_x0));
        scissor.top = static_cast<LONG>((std::max)(0.f, cmd.clip_y0));
        scissor.right = static_cast<LONG>((std::min)(vp_w, cmd.clip_x1));
        scissor.bottom = static_cast<LONG>((std::min)(vp_h, cmd.clip_y1));
        if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) {
            continue;
        }
        command_list_->RSSetScissorRects(1, &scissor);
        command_list_->DrawIndexedInstanced(cmd.index_count, 1, cmd.index_offset, 0, 0);
    }

    command_list_->RSSetScissorRects(1, &full_scissor);
    return Status::Ok();
}

Status D3D12Device::SetupScreenQuads(const std::filesystem::path& vs_path,
                                                const std::filesystem::path& ps_path) {
    auto vs = ReadFileBytes(vs_path);
    if (!vs) {
        return vs.status();
    }
    auto ps = ReadFileBytes(ps_path);
    if (!ps) {
        return ps.status();
    }

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
        return Status::Fail(std::string("Quad root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                        IID_PPV_ARGS(&quad_root_));
    if (FAILED(hr)) {
        return Status::Fail("Create quad root signature failed");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = quad_root_.Get();
    pso.VS = {vs->data(), vs->size()};
    pso.PS = {ps->data(), ps->size()};
    pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.InputLayout = {layout, 2};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    hr = device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&quad_pso_));
    if (FAILED(hr)) {
        return Status::Fail("Create quad PSO failed: " + HrToString(hr));
    }

    // Preallocate once per frame slot so DrawScreenQuads never grows mid-frame.
    constexpr UINT kMaxQuads = 1024;
    constexpr UINT kVertBytes = kMaxQuads * 6 * (2 + 4) * sizeof(float);  // pos2 + color4
    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = kVertBytes;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (std::uint32_t fi = 0; fi < kFrameCount; ++fi) {
        quad_vb_[fi].Reset();
        hr = device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf,
                                                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                    IID_PPV_ARGS(&quad_vb_[fi]));
        if (FAILED(hr)) {
            return Status::Fail("Create quad VB failed: " + HrToString(hr));
        }
    }
    quad_vb_capacity_ = kVertBytes;

    quad_ready_ = true;
    return Status::Ok();
}

Status D3D12Device::SetupDebugLines(const std::filesystem::path& vs_path,
                                             const std::filesystem::path& ps_path) {
    auto vs = ReadFileBytes(vs_path);
    if (!vs) {
        return vs.status();
    }
    auto ps = ReadFileBytes(ps_path);
    if (!ps) {
        return ps.status();
    }

    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    param.Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 1;
    rs.pParameters = &param;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
        return Status::Fail(std::string("Debug root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                        IID_PPV_ARGS(&debug_root_));
    if (FAILED(hr)) {
        return Status::Fail("Create debug root signature failed");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = debug_root_.Get();
    pso.VS = {vs->data(), vs->size()};
    pso.PS = {ps->data(), ps->size()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.InputLayout = {layout, 2};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    hr = device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&debug_pso_));
    if (FAILED(hr)) {
        return Status::Fail("Create debug PSO failed: " + HrToString(hr));
    }

    debug_cb_.Reset();
    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = 256ull * kFrameCount;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf,
                                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                IID_PPV_ARGS(&debug_cb_));
    if (FAILED(hr)) {
        return Status::Fail("Create debug CB failed");
    }

    // Preallocate per-frame debug VB (grid+axes+AABBs); never Reset mid-frame.
    constexpr UINT kMaxDebugVerts = 16384;
    constexpr UINT kDebugVbBytes =
            kMaxDebugVerts * static_cast<UINT>(sizeof(DebugLineVertex));
    D3D12_RESOURCE_DESC vbdesc = buf;
    vbdesc.Width = kDebugVbBytes;
    for (std::uint32_t fi = 0; fi < kFrameCount; ++fi) {
        debug_vb_[fi].Reset();
        hr = device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &vbdesc,
                                                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                    IID_PPV_ARGS(&debug_vb_[fi]));
        if (FAILED(hr)) {
            return Status::Fail("Create debug VB failed");
        }
    }
    debug_vb_capacity_ = kDebugVbBytes;

    debug_ready_ = true;
    LogInfo("Debug line path ready");
    return Status::Ok();
}

}  // namespace engine::rhi
