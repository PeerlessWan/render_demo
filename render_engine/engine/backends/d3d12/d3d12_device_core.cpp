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

Status D3D12Device::ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) {
  w = static_cast<int>(width_);
  h = static_cast<int>(height_);
  if (w <= 0 || h <= 0 || !command_list_ || !device_ || !backbuffers_[0]) {
    return Status::Fail("Readback: device not ready");
  }
  if (auto st = EnsureColorReadbackBuffer(); !st) {
    return st;
  }

  const auto bb_index = CurrentBbIndex();
  auto* backbuffer = backbuffers_[bb_index].Get();
  const D3D12_RESOURCE_STATES before = backbuffer_states_[bb_index];
  if (before != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    Transition(backbuffer, before, D3D12_RESOURCE_STATE_COPY_SOURCE);
    backbuffer_states_[bb_index] = D3D12_RESOURCE_STATE_COPY_SOURCE;
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT num_rows = 0;
  UINT64 row_size = 0;
  UINT64 total = 0;
  const D3D12_RESOURCE_DESC src_desc = backbuffer->GetDesc();
  device_->GetCopyableFootprints(&src_desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total);

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = color_readback_.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = backbuffer;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  backbuffer_states_[bb_index] = D3D12_RESOURCE_STATE_RENDER_TARGET;

  if (FAILED(command_list_->Close())) {
    return Status::Fail("Readback Close failed");
  }
  ID3D12CommandList* lists[] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  WaitGpu();

  const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  out_rgba.assign(pixels * 4, 0);
  void* mapped = nullptr;
  D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
  if (FAILED(color_readback_->Map(0, &range, &mapped)) || !mapped) {
    return Status::Fail("Readback Map failed");
  }
  const auto* src_bytes = static_cast<const std::uint8_t*>(mapped);
  const std::size_t pitch = footprint.Footprint.RowPitch;
  const std::size_t row_bytes = static_cast<std::size_t>(w) * 4u;
  for (int y = 0; y < h; ++y) {
    const auto* row = src_bytes + static_cast<std::size_t>(y) * pitch;
    std::memcpy(out_rgba.data() + static_cast<std::size_t>(y) * row_bytes, row, row_bytes);
  }
  color_readback_->Unmap(0, nullptr);

  const auto frame = frame_index_;
  if (FAILED(allocators_[frame]->Reset())) {
    return Status::Fail("Readback allocator Reset failed");
  }
  if (FAILED(command_list_->Reset(allocators_[frame].Get(), nullptr))) {
    return Status::Fail("Readback command list Reset failed");
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
                                        static_cast<SIZE_T>(bb_index) * rtv_descriptor_size_};
  if (dsv_) {
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  } else {
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
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

Status D3D12Device::ReadbackDepthRgbaStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) {
  w = static_cast<int>(width_);
  h = static_cast<int>(height_);
  if (w <= 0 || h <= 0 || !command_list_ || !device_ || !dsv_) {
    return Status::Fail("Depth readback: device/depth not ready");
  }
  if (auto st = EnsureDepthReadbackBuffer(); !st) {
    return st;
  }

  const D3D12_RESOURCE_STATES prev = depth_state_;
  if (prev != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    Transition(dsv_.Get(), prev, D3D12_RESOURCE_STATE_COPY_SOURCE);
    depth_state_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
  }

  D3D12_RESOURCE_DESC src_desc = dsv_->GetDesc();
  src_desc.Format = DXGI_FORMAT_R32_FLOAT;  // typeless depth → float copy
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT num_rows = 0;
  UINT64 row_size = 0;
  UINT64 total = 0;
  device_->GetCopyableFootprints(&src_desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total);
  footprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = depth_readback_.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = dsv_.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  Transition(dsv_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

  if (FAILED(command_list_->Close())) {
    return Status::Fail("Depth readback Close failed");
  }
  ID3D12CommandList* lists[] = {command_list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  WaitGpu();

  const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  out_rgba.assign(pixels * 4, 0);
  void* mapped = nullptr;
  D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
  if (FAILED(depth_readback_->Map(0, &range, &mapped)) || !mapped) {
    return Status::Fail("Depth readback Map failed");
  }
  const auto* src_bytes = static_cast<const std::uint8_t*>(mapped);
  const std::size_t pitch = footprint.Footprint.RowPitch;
  for (int y = 0; y < h; ++y) {
    const auto* row = src_bytes + static_cast<std::size_t>(y) * pitch;
    for (int x = 0; x < w; ++x) {
      float d = 0.f;
      std::memcpy(&d, row + static_cast<std::size_t>(x) * 4, sizeof(float));
      if (!(d == d)) {  // NaN
        d = 1.f;
      }
      d = (std::min)(1.f, (std::max)(0.f, d));
      // Visualize: near → bright (invert typical 0..1 depth).
      const auto g = static_cast<std::uint8_t>((1.f - d) * 255.f + 0.5f);
      const std::size_t di = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                              static_cast<std::size_t>(x)) *
                             4;
      out_rgba[di + 0] = g;
      out_rgba[di + 1] = g;
      out_rgba[di + 2] = g;
      out_rgba[di + 3] = 255;
    }
  }
  depth_readback_->Unmap(0, nullptr);

  const auto frame = frame_index_;
  if (FAILED(allocators_[frame]->Reset())) {
    return Status::Fail("Depth readback allocator Reset failed");
  }
  if (FAILED(command_list_->Reset(allocators_[frame].Get(), nullptr))) {
    return Status::Fail("Depth readback command list Reset failed");
  }
  const auto bb_index = CurrentBbIndex();
  const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
                                        static_cast<SIZE_T>(bb_index) * rtv_descriptor_size_};
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  D3D12_VIEWPORT vp{};
  vp.Width = static_cast<float>(width_);
  vp.Height = static_cast<float>(height_);
  vp.MaxDepth = 1.f;
  command_list_->RSSetViewports(1, &vp);
  D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
  command_list_->RSSetScissorRects(1, &scissor);
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

Status D3D12Device::EnsureColorReadbackBuffer() {
  if (color_readback_ && color_readback_w_ == width_ && color_readback_h_ == height_) {
    return Status::Ok();
  }
  color_readback_.Reset();
  color_readback_w_ = width_;
  color_readback_h_ = height_;
  D3D12_RESOURCE_DESC src{};
  src.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  src.Width = width_;
  src.Height = height_;
  src.DepthOrArraySize = 1;
  src.MipLevels = 1;
  src.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  src.SampleDesc.Count = 1;
  src.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 total = 0;
  device_->GetCopyableFootprints(&src, 0, 1, 0, &footprint, nullptr, nullptr, &total);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buf{};
  buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buf.Width = total;
  buf.Height = 1;
  buf.DepthOrArraySize = 1;
  buf.MipLevels = 1;
  buf.SampleDesc.Count = 1;
  buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  const HRESULT hr =
      device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                       IID_PPV_ARGS(&color_readback_));
  if (FAILED(hr)) {
    return Status::Fail("Create color readback failed: " + HrToString(hr));
  }
  return Status::Ok();
}

Status D3D12Device::EnsureDepthReadbackBuffer() {
  if (depth_readback_ && depth_readback_w_ == width_ && depth_readback_h_ == height_) {
    return Status::Ok();
  }
  depth_readback_.Reset();
  depth_readback_w_ = width_;
  depth_readback_h_ = height_;
  D3D12_RESOURCE_DESC src{};
  src.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  src.Width = width_;
  src.Height = height_;
  src.DepthOrArraySize = 1;
  src.MipLevels = 1;
  src.Format = DXGI_FORMAT_R32_FLOAT;
  src.SampleDesc.Count = 1;
  src.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 total = 0;
  device_->GetCopyableFootprints(&src, 0, 1, 0, &footprint, nullptr, nullptr, &total);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buf{};
  buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buf.Width = total;
  buf.Height = 1;
  buf.DepthOrArraySize = 1;
  buf.MipLevels = 1;
  buf.SampleDesc.Count = 1;
  buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  const HRESULT hr =
      device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                       IID_PPV_ARGS(&depth_readback_));
  if (FAILED(hr)) {
    return Status::Fail("Create depth readback failed: " + HrToString(hr));
  }
  return Status::Ok();
}

Status D3D12Device::CreateGpuTimestampResources() {
  D3D12_QUERY_HEAP_DESC qh{};
  qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  qh.Count = kMaxTimestampQueries;
  HRESULT hr = device_->CreateQueryHeap(&qh, IID_PPV_ARGS(&timestamp_heap_));
  if (FAILED(hr)) {
    return Status::Fail("CreateQueryHeap(TIMESTAMP) failed: " + HrToString(hr));
  }

  D3D12_HEAP_PROPERTIES readback{};
  readback.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buf{};
  buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buf.Width = sizeof(UINT64) * kMaxTimestampQueries * kFrameCount;
  buf.Height = 1;
  buf.DepthOrArraySize = 1;
  buf.MipLevels = 1;
  buf.SampleDesc.Count = 1;
  buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  hr = device_->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &buf,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&timestamp_readback_));
  if (FAILED(hr)) {
    return Status::Fail("Create timestamp readback failed: " + HrToString(hr));
  }

  hr = queue_->GetTimestampFrequency(&timestamp_freq_);
  if (FAILED(hr) || timestamp_freq_ == 0) {
    return Status::Fail("GetTimestampFrequency failed: " + HrToString(hr));
  }
  return Status::Ok();
}

void D3D12Device::ReadbackGpuPassTimings(std::uint32_t frame) {
  if (!frame_timestamps_pending_[frame] || !timestamp_readback_ || timestamp_freq_ == 0) {
    return;
  }
  const UINT passes = frame_gpu_pass_counts_[frame];
  const UINT query_count = passes * 2;
  if (passes == 0 || query_count > kMaxTimestampQueries) {
    frame_timestamps_pending_[frame] = false;
    last_gpu_timings_.clear();
    return;
  }

  const SIZE_T byte_offset =
      static_cast<SIZE_T>(frame) * kMaxTimestampQueries * sizeof(UINT64);
  const SIZE_T byte_size = static_cast<SIZE_T>(query_count) * sizeof(UINT64);
  D3D12_RANGE read_range{byte_offset, byte_offset + byte_size};
  void* mapped = nullptr;
  if (FAILED(timestamp_readback_->Map(0, &read_range, &mapped)) || !mapped) {
    frame_timestamps_pending_[frame] = false;
    return;
  }

  const auto* stamps =
      reinterpret_cast<const UINT64*>(static_cast<const char*>(mapped) + byte_offset);
  last_gpu_timings_.clear();
  last_gpu_timings_.reserve(passes);
  for (UINT i = 0; i < passes; ++i) {
    const UINT64 t0 = stamps[i * 2];
    const UINT64 t1 = stamps[i * 2 + 1];
    GpuPassTiming timing;
    timing.name = frame_gpu_pass_names_[frame][i];
    timing.ms = (t1 >= t0 && timestamp_freq_ > 0)
                    ? (static_cast<double>(t1 - t0) * 1000.0 /
                       static_cast<double>(timestamp_freq_))
                    : 0.0;
    last_gpu_timings_.push_back(std::move(timing));
  }

  D3D12_RANGE written{0, 0};
  timestamp_readback_->Unmap(0, &written);
  frame_timestamps_pending_[frame] = false;
}

}  // namespace engine::rhi
