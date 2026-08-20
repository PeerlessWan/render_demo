#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::CreateSwapchain() {
    allow_tearing_ = false;
    BOOL allow = FALSE;
    if (SUCCEEDED(factory_->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow,
                                                                                                sizeof(allow)))) {
        allow_tearing_ = allow == TRUE;
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width_;
    scd.Height = height_;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = kFrameCount;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (allow_tearing_) {
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    ComPtr<IDXGISwapChain1> swap1;
    HRESULT hr =
            factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd_, &scd, nullptr, nullptr, &swap1);
    if (FAILED(hr)) {
        return Status::Fail("CreateSwapChainForHwnd failed: " + HrToString(hr));
    }
    factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    hr = swap1.As(&swapchain_);
    if (FAILED(hr)) {
        return Status::Fail("QueryInterface IDXGISwapChain3 failed");
    }
    LogInfo(std::string("D3D12 vsync=") + (vsync_ ? "on" : "off") +
                    (allow_tearing_ ? " (tearing OK)" : ""));
    return Status::Ok();
}

Status D3D12Device::CreateOffscreenBackbuffers() {
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        backbuffers_[i].Reset();
        backbuffer_states_[i] = D3D12_RESOURCE_STATE_PRESENT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width_;
        desc.Height = height_;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        clear.Color[0] = 0.f;
        clear.Color[1] = 0.f;
        clear.Color[2] = 0.f;
        clear.Color[3] = 1.f;
        const HRESULT hr = device_->CreateCommittedResource(
                &heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PRESENT, &clear,
                IID_PPV_ARGS(&backbuffers_[i]));
        if (FAILED(hr)) {
            return Status::Fail("Create offscreen backbuffer failed: " + HrToString(hr));
        }
    }
    offscreen_bb_index_ = 0;
    return Status::Ok();
}

void D3D12Device::TryEnableDisplayHdr() {
    hdr_output_active_ = false;
    if (!swapchain_) {
        return;
    }
    ComPtr<IDXGISwapChain3> sc3;
    if (FAILED(swapchain_.As(&sc3)) || !sc3) {
        return;
    }
    // Prefer HDR10 if the output supports it; keep SDR swapchain format for compatibility.
    const HRESULT hr = sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    if (SUCCEEDED(hr)) {
        hdr_output_active_ = true;
        LogInfo("Display HDR10 color space enabled");
    }
}

Status D3D12Device::CreateFrameResources() {
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = kFrameCount;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HRESULT hr = device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("CreateDescriptorHeap(RTV) failed");
    }
    rtv_descriptor_size_ =
            device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                                 IID_PPV_ARGS(&allocators_[i]));
        if (FAILED(hr)) {
            return Status::Fail("CreateCommandAllocator failed");
        }
        fence_values_[i] = 0;
    }

    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(),
                                                                    nullptr, IID_PPV_ARGS(&command_list_));
    if (FAILED(hr)) {
        return Status::Fail("CreateCommandList failed");
    }
    command_list_->Close();

    return CreateRenderTargets();
}

Status D3D12Device::CreateRenderTargets() {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        if (!gpu_headless_) {
            backbuffers_[i].Reset();
            HRESULT hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i]));
            if (FAILED(hr)) {
                return Status::Fail("SwapChain::GetBuffer failed");
            }
            // DXGI flip-model buffers are created in COMMON until the first Present.
            backbuffer_states_[i] = D3D12_RESOURCE_STATE_COMMON;
        } else if (!backbuffers_[i]) {
            return Status::Fail("Offscreen backbuffer missing");
        }
        device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, handle);
        handle.ptr += rtv_descriptor_size_;
    }
    frame_index_ = 0;
    if (gpu_headless_) {
        offscreen_bb_index_ = 0;
    }
    return Status::Ok();
}

Status D3D12Device::CreateDepthBuffer() {
    dsv_.Reset();
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    HRESULT hr = device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("CreateDescriptorHeap(DSV) failed");
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    hr = device_->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                                                                D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                                                IID_PPV_ARGS(&dsv_));
    if (FAILED(hr)) {
        return Status::Fail("Create depth resource failed");
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(dsv_.Get(), &dsv,
                                                                    dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    if (post_srv_heap_) {
        UpdatePostSrvs();
    }
    return Status::Ok();
}

Status D3D12Device::CreateVertexBuffer() {
    const Vertex verts[] = {
            {0.0f, 0.6f, 0.0f, 1.f, 0.2f, 0.2f, 0.5f, 0.0f},
            {0.6f, -0.6f, 0.0f, 0.2f, 1.f, 0.2f, 1.0f, 1.0f},
            {-0.6f, -0.6f, 0.0f, 0.2f, 0.2f, 1.f, 0.0f, 1.0f},
    };
    const UINT bytes = sizeof(verts);

    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = bytes;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf,
                                                                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                                                IID_PPV_ARGS(&vertex_buffer_));
    if (FAILED(hr)) {
        return Status::Fail("Create vertex buffer failed");
    }
    void* mapped = nullptr;
    D3D12_RANGE no_read{0, 0};
    vertex_buffer_->Map(0, &no_read, &mapped);
    std::memcpy(mapped, verts, bytes);
    vertex_buffer_->Unmap(0, nullptr);

    vbv_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = bytes;
    vbv_.StrideInBytes = sizeof(Vertex);
    return Status::Ok();
}

Status D3D12Device::CreateCheckerTexture() {
    constexpr UINT w = 2;
    constexpr UINT h = 2;
    const std::uint8_t pixels[w * h * 4] = {
            255, 255, 255, 255, 40, 40, 40, 255, 40, 40, 40, 255, 255, 255, 255, 255,
    };

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
                                                                                                IID_PPV_ARGS(&texture_));
    if (FAILED(hr)) {
        return Status::Fail("Create texture failed");
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
        return Status::Fail("Create texture upload failed");
    }

    D3D12_SUBRESOURCE_DATA sub{};
    sub.pData = pixels;
    sub.RowPitch = w * 4;
    sub.SlicePitch = sub.RowPitch * h;

    // Use a one-shot command list on allocator 0 (must be idle).
    WaitGpu();
    allocators_[0]->Reset();
    command_list_->Reset(allocators_[0].Get(), nullptr);

    // Manual upload copy (avoid d3dx12 UpdateSubresources dependency).
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
        std::memcpy(dst + y * layout.Footprint.RowPitch, pixels + y * w * 4, w * 4);
    }
    upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource = texture_.Get();
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource = upload.Get();
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = layout;
    command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

    Transition(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list_->Close();
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitGpu();

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap{};
    srv_heap.NumDescriptors = 1;
    srv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srv_heap, IID_PPV_ARGS(&srv_heap_));
    if (FAILED(hr)) {
        return Status::Fail("Create SRV heap failed");
    }
    device_->CreateShaderResourceView(texture_.Get(), nullptr,
                                                                        srv_heap_->GetCPUDescriptorHandleForHeapStart());
    texture_upload_ = upload;  // keep alive
    return Status::Ok();
}

}  // namespace engine::rhi
