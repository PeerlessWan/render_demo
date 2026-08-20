#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::Init(const DeviceDesc& desc) {
    if (desc.width == 0 || desc.height == 0) {
        return Status::Fail(ErrorCode::InvalidArgument, "Invalid DeviceDesc");
    }
    gpu_headless_ = desc.gpu_headless;
    enable_hdr_output_ = desc.enable_hdr_output && !desc.gpu_headless;
    if (!gpu_headless_ && !desc.native_window) {
        return Status::Fail(ErrorCode::InvalidArgument, "Invalid DeviceDesc (missing HWND)");
    }
    hwnd_ = desc.native_window ? static_cast<HWND>(desc.native_window) : nullptr;
    width_ = desc.width;
    height_ = desc.height;
    adapter_index_ = desc.adapter_index;
    vsync_ = desc.enable_vsync;

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
        }
    }
#else
    if (desc.enable_validation) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            LogInfo("D3D12 validation layer enabled (enable_validation)");
        } else {
            LogWarn("D3D12 validation requested but debug interface unavailable — SKIP");
        }
    }
#endif

    ComPtr<IDXGIFactory6> factory;
    UINT factory_flags = 0;
#if defined(_DEBUG)
    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            return Status::Fail("CreateDXGIFactory2 failed: " + HrToString(hr));
        }
    }
    factory_ = factory;

    ComPtr<IDXGIAdapter1> adapter;
    auto try_create = [&](IDXGIAdapter1* a) -> bool {
        return a && SUCCEEDED(D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                                                                        nullptr));
    };

    if (adapter_index_ >= 0) {
        if (FAILED(factory->EnumAdapters1(static_cast<UINT>(adapter_index_), &adapter)) ||
                !try_create(adapter.Get())) {
            return Status::Fail("D3D12 adapter_index=" + std::to_string(adapter_index_) +
                                                    " unavailable or not D3D12-capable");
        }
    } else {
        for (UINT i = 0;
                 factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                                         IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
                 ++i) {
            DXGI_ADAPTER_DESC1 ad{};
            adapter->GetDesc1(&ad);
            if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter.Reset();
                continue;
            }
            if (try_create(adapter.Get())) {
                break;
            }
            adapter.Reset();
        }
        if (!adapter) {
            if (FAILED(factory->EnumAdapters1(0, &adapter)) || !try_create(adapter.Get())) {
                return Status::Fail("No DXGI adapter");
            }
        }
    }

    {
        DXGI_ADAPTER_DESC1 ad{};
        adapter->GetDesc1(&ad);
        char name_utf8[256]{};
        WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name_utf8, sizeof(name_utf8), nullptr,
                                                nullptr);
        LogInfo(std::string("D3D12 adapter[") + std::to_string(adapter_index_ >= 0 ? adapter_index_ : -1) +
                        " auto]: " + name_utf8);
    }

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) {
        return Status::Fail("D3D12CreateDevice failed: " + HrToString(hr));
    }

    D3D12_COMMAND_QUEUE_DESC qdesc{};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device_->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue_));
    if (FAILED(hr)) {
        return Status::Fail("CreateCommandQueue failed: " + HrToString(hr));
    }

    if (auto st = CreateGpuTimestampResources(); !st) {
        return st;
    }

    if (gpu_headless_) {
        if (auto st = CreateOffscreenBackbuffers(); !st) {
            return st;
        }
    } else {
        if (auto st = CreateSwapchain(); !st) {
            return st;
        }
    }
    if (auto st = CreateFrameResources(); !st) {
        return st;
    }

    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) {
        return Status::Fail("CreateFence failed: " + HrToString(hr));
    }
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) {
        return Status::Fail("CreateEventW failed");
    }

    // Extra allocators for M14 multithread_submit skeleton (still serial Execute).
    for (int i = 0; i < 4; ++i) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                                 IID_PPV_ARGS(&worker_allocators_[static_cast<std::size_t>(i)]));
        if (FAILED(hr)) {
            return Status::Fail("Create worker CommandAllocator failed");
        }
    }

    if (enable_hdr_output_) {
        TryEnableDisplayHdr();
    }
    // Bindless capability Feature: ResourceBindingTier>=2 → QueryFeature("bindless").
    // Hot-path albedo via ResourceDescriptorHeap stays OFF by default (pad=-1 classic
    // t1/t4) so golden/C4 do not drift. Opt-in: SetFeatureOverride("bindless_hot_path", true)
    // when capable and not gpu_headless (see BindlessAlbedoHeapPad).
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS opts{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts))) &&
                opts.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2) {
            bindless_capable_ = true;
            engine::SetFeatureOverride("bindless", true);
            LogInfo("D3D12 bindless Feature path enabled (ResourceBindingTier>=2); "
                            "bindless_hot_path default OFF (classic pad=-1)");
        } else {
            bindless_capable_ = false;
            LogWarn("D3D12 bindless SKIP (ResourceBindingTier < 2)");
        }
    }
    engine::SetFeatureOverride("multithread_submit", true);
    if (hdr_output_active_) {
        engine::SetFeatureOverride("hdr_output", true);
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5,
                                                                                         sizeof(opts5))) &&
            opts5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
        engine::SetFeatureOverride("raytracing", true);
        LogInfo("D3D12 raytracing Feature enabled (OPTIONS5 RaytracingTier)");
    } else {
        engine::SetFeatureOverride("raytracing", false);
        LogInfo("D3D12 raytracing SKIP (OPTIONS5 tier not supported)");
    }

    LogInfo(gpu_headless_ ? "D3D12 device ready (gpu_headless offscreen)" : "D3D12 device ready");
    return Status::Ok();
}

[[nodiscard]] bool D3D12Device::is_headless() const { return gpu_headless_; }

[[nodiscard]] DeviceApiKind D3D12Device::api_kind() const { return DeviceApiKind::D3D12; }

void D3D12Device::SetVSync(bool enabled) { vsync_ = enabled; }

[[nodiscard]] bool D3D12Device::vsync() const { return vsync_; }

UINT D3D12Device::CurrentBbIndex() const {
    if (gpu_headless_ || !swapchain_) {
        return offscreen_bb_index_;
    }
    return swapchain_->GetCurrentBackBufferIndex();
}

D3D12Device::~D3D12Device() {
    WaitGpu();
    if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    engine::ClearFeatureOverrides();
}

std::uint32_t D3D12Device::width() const { return width_; }

std::uint32_t D3D12Device::height() const { return height_; }

Status D3D12Device::BeginFrame() {
    const auto frame = frame_index_;
    const UINT64 fence_to_wait = fence_values_[frame];
    if (fence_->GetCompletedValue() < fence_to_wait) {
        if (FAILED(fence_->SetEventOnCompletion(fence_to_wait, fence_event_))) {
            return Status::Fail("SetEventOnCompletion failed");
        }
        WaitForSingleObject(fence_event_, INFINITE);
    }

    ReadbackGpuPassTimings(frame);

    timestamp_cursor_ = 0;
    gpu_pass_count_ = 0;
    post_resolved_this_frame_ = false;

    if (FAILED(allocators_[frame]->Reset())) {
        return Status::Fail("CommandAllocator::Reset failed");
    }
    if (FAILED(command_list_->Reset(allocators_[frame].Get(), nullptr))) {
        return Status::Fail("CommandList::Reset failed");
    }

    const auto bb = CurrentBbIndex();
    auto* backbuffer = backbuffers_[bb].Get();
    const D3D12_RESOURCE_STATES before = backbuffer_states_[bb];
    if (before != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        Transition(backbuffer, before, D3D12_RESOURCE_STATE_RENDER_TARGET);
        backbuffer_states_[bb] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    return Status::Ok();
}

Status D3D12Device::Clear(const ColorRgba& color) {
    last_clear_ = color;
    const auto index = CurrentBbIndex();
    const D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv{
            rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
            static_cast<SIZE_T>(index) * rtv_descriptor_size_};
    const float clear[4] = {color.r, color.g, color.b, color.a};
    // Keep swapchain cleared for UI/debug; lit geometry renders into HDR scene_color_.
    command_list_->ClearRenderTargetView(bb_rtv, clear, 0, nullptr);

    if (!scene_color_ || !hdr_rtv_heap_) {
        if (dsv_) {
            command_list_->ClearDepthStencilView(dsv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                                                                     D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
            command_list_->OMSetRenderTargets(1, &bb_rtv, FALSE, &dsv);
        } else {
            command_list_->OMSetRenderTargets(1, &bb_rtv, FALSE, nullptr);
        }
    } else {
        if (scene_color_state_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            Transition(scene_color_.Get(), scene_color_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
            scene_color_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE hdr_rtv =
                hdr_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        command_list_->ClearRenderTargetView(hdr_rtv, clear, 0, nullptr);
        if (dsv_) {
            command_list_->ClearDepthStencilView(dsv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                                                                     D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
            command_list_->OMSetRenderTargets(1, &hdr_rtv, FALSE, &dsv);
        } else {
            command_list_->OMSetRenderTargets(1, &hdr_rtv, FALSE, nullptr);
        }
    }

    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &vp);
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    command_list_->RSSetScissorRects(1, &scissor);
    return Status::Ok();
}

Status D3D12Device::DrawSimpleMesh() {
    if (!pso_) {
        return Status::Fail("SetupSimpleMesh not called");
    }
    command_list_->SetPipelineState(pso_.Get());
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootDescriptorTable(0,
                                                                                                srv_heap_->GetGPUDescriptorHandleForHeapStart());

    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW vbv = vbv_;
    command_list_->IASetVertexBuffers(0, 1, &vbv);
    command_list_->DrawInstanced(3, 1, 0, 0);
    return Status::Ok();
}

Status D3D12Device::SetSubmitConfig(const SubmitConfig& cfg) {
    if (auto st = ValidateSubmitConfig(cfg); !st) {
        return st;
    }
    submit_cfg_ = cfg;
    return Status::Ok();
}

Status D3D12Device::Present() {
    const auto bb = CurrentBbIndex();
    auto* backbuffer = backbuffers_[bb].Get();
    if (backbuffer_states_[bb] != D3D12_RESOURCE_STATE_PRESENT) {
        Transition(backbuffer, backbuffer_states_[bb], D3D12_RESOURCE_STATE_PRESENT);
        backbuffer_states_[bb] = D3D12_RESOURCE_STATE_PRESENT;
    }

    if (timestamp_heap_ && timestamp_readback_ && timestamp_cursor_ > 0) {
        const UINT64 dest_offset =
                static_cast<UINT64>(frame_index_) * kMaxTimestampQueries * sizeof(UINT64);
        command_list_->ResolveQueryData(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                                                                        timestamp_cursor_, timestamp_readback_.Get(), dest_offset);
        frame_gpu_pass_counts_[frame_index_] = gpu_pass_count_;
        for (UINT i = 0; i < gpu_pass_count_ && i < kMaxGpuPasses; ++i) {
            frame_gpu_pass_names_[frame_index_][i] = gpu_pass_names_[i];
        }
        frame_timestamps_pending_[frame_index_] = true;
    }

    if (FAILED(command_list_->Close())) {
        std::string detail = "CommandList::Close failed";
        ComPtr<ID3D12InfoQueue> iq;
        if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&iq))) && iq) {
            const UINT64 n = iq->GetNumStoredMessages();
            const UINT64 start = n > 12 ? n - 12 : 0;
            for (UINT64 i = start; i < n; ++i) {
                SIZE_T len = 0;
                if (FAILED(iq->GetMessage(i, nullptr, &len)) || len == 0) {
                    continue;
                }
                std::vector<std::uint8_t> bytes(len);
                auto* msg = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                if (SUCCEEDED(iq->GetMessage(i, msg, &len)) && msg->pDescription) {
                    detail += "\n  [D3D12] ";
                    detail += msg->pDescription;
                }
            }
        }
        return Status::Fail(detail);
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    if (!gpu_headless_) {
        UINT sync = vsync_ ? 1u : 0u;
        UINT flags = 0;
        if (!vsync_ && allow_tearing_) {
            flags = DXGI_PRESENT_ALLOW_TEARING;
        }
        const HRESULT hr = swapchain_->Present(sync, flags);
        if (FAILED(hr)) {
            std::string msg = "Present failed: " + HrToString(hr);
            if (device_) {
                const HRESULT removed = device_->GetDeviceRemovedReason();
                msg += " removed=" + HrToString(removed);
            }
            return Status::Fail(msg);
        }
    } else {
        offscreen_bb_index_ = (offscreen_bb_index_ + 1) % kFrameCount;
    }

    const UINT64 signal = ++fence_value_;
    if (FAILED(queue_->Signal(fence_.Get(), signal))) {
        return Status::Fail("Queue::Signal failed");
    }
    fence_values_[frame_index_] = signal;
    frame_index_ = (frame_index_ + 1) % kFrameCount;
    return Status::Ok();
}

Status D3D12Device::Resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return Status::Ok();
    }
    if (width == width_ && height == height_) {
        return Status::Ok();
    }
    WaitGpu();
    width_ = width;
    height_ = height;
    for (auto& bb : backbuffers_) {
        bb.Reset();
    }
    dsv_.Reset();
    if (gpu_headless_) {
        if (auto st = CreateOffscreenBackbuffers(); !st) {
            return st;
        }
    } else {
        DXGI_SWAP_CHAIN_DESC1 scd{};
        swapchain_->GetDesc1(&scd);
        const HRESULT hr =
                swapchain_->ResizeBuffers(kFrameCount, width_, height_, scd.Format, scd.Flags);
        if (FAILED(hr)) {
            return Status::Fail("ResizeBuffers failed: " + HrToString(hr));
        }
    }
    if (auto st = CreateRenderTargets(); !st) {
        return st;
    }
    if (auto st = CreateDepthBuffer(); !st) {
        return st;
    }
    if (post_ready_) {
        if (auto st = CreatePostColorTargets(); !st) {
            return st;
        }
        UpdatePostSrvs();
    }
    return Status::Ok();
}

Status D3D12Device::SetupSimpleMesh(const SimpleMeshShaders& shaders) {
    WaitGpu();

    auto vs = ReadFileBytes(shaders.vs_dxil);
    if (!vs) {
        return vs.status();
    }
    auto ps = ReadFileBytes(shaders.ps_dxil);
    if (!ps) {
        return ps.status();
    }

    // Root signature: table(t0) + static sampler s0
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 1;
    rs.pParameters = &param;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &sampler;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
        return Status::Fail(std::string("SerializeRootSignature failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                        IID_PPV_ARGS(&root_signature_));
    if (FAILED(hr)) {
        return Status::Fail("CreateRootSignature failed");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_signature_.Get();
    pso.VS = {vs->data(), vs->size()};
    pso.PS = {ps->data(), ps->size()};
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.InputLayout = {layout, 3};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;

    hr = device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pso_));
    if (FAILED(hr)) {
        return Status::Fail("CreateGraphicsPipelineState failed: " + HrToString(hr));
    }

    if (auto st = CreateDepthBuffer(); !st) {
        return st;
    }
    if (auto st = CreateVertexBuffer(); !st) {
        return st;
    }
    if (auto st = CreateCheckerTexture(); !st) {
        return st;
    }

    mesh_ready_ = true;
    LogInfo("Simple textured triangle mesh ready");
    return Status::Ok();
}

void D3D12Device::SetDrawViewport(float x, float y, float w, float h) {
    draw_vp_x_ = x;
    draw_vp_y_ = y;
    draw_vp_w_ = w;
    draw_vp_h_ = h;
    draw_vp_on_ = w > 1.f && h > 1.f;
}

void D3D12Device::SetPreferLdrTarget(bool on) { prefer_ldr_ = on; }

void D3D12Device::GpuPassBegin(const char* name) {
    if (!timestamp_heap_ || !command_list_) {
        return;
    }
    if (timestamp_cursor_ + 2 > kMaxTimestampQueries || gpu_pass_count_ >= kMaxGpuPasses) {
        return;
    }
    command_list_->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestamp_cursor_);
    gpu_pass_names_[gpu_pass_count_] = name ? name : "";
    ++gpu_pass_count_;
    ++timestamp_cursor_;
}

void D3D12Device::GpuPassEnd() {
    if (!timestamp_heap_ || !command_list_) {
        return;
    }
    if (timestamp_cursor_ >= kMaxTimestampQueries) {
        return;
    }
    command_list_->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestamp_cursor_);
    ++timestamp_cursor_;
}

[[nodiscard]] bool D3D12Device::GpuTimestampAvailable() const {
    return timestamp_heap_ != nullptr && timestamp_freq_ > 0;
}

[[nodiscard]] std::vector<GpuPassTiming> D3D12Device::LastGpuPassTimings() const {
    return last_gpu_timings_;
}

UINT64 D3D12Device::ObjectCbOffset(std::size_t draw_index) const {
    return (static_cast<UINT64>(frame_index_) * kMaxLitDraws +
                    static_cast<UINT64>(draw_index % kMaxLitDraws)) *
                 256ull;
}

void D3D12Device::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &barrier);
}

void D3D12Device::WaitGpuSubmitted() {
    if (!queue_ || !fence_) {
        return;
    }
    const UINT64 signal = ++fence_value_;
    if (SUCCEEDED(queue_->Signal(fence_.Get(), signal))) {
        if (fence_->GetCompletedValue() < signal) {
            fence_->SetEventOnCompletion(signal, fence_event_);
            WaitForSingleObject(fence_event_, INFINITE);
        }
    }
}

void D3D12Device::WaitGpu() {
    WaitGpuSubmitted();
    for (auto& v : fence_values_) {
        v = fence_value_;
    }
}

}  // namespace engine::rhi
