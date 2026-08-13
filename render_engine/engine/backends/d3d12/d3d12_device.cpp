#include "engine/rhi/i_device.h"

#include "engine/core/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace engine::rhi {
namespace {

constexpr std::uint32_t kFrameCount = 2;

std::string HrToString(HRESULT hr) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(hr));
  return buf;
}

Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return Result<std::vector<std::uint8_t>>::Fail("Cannot open shader: " + path.string());
  }
  const auto size = in.tellg();
  if (size <= 0) {
    return Result<std::vector<std::uint8_t>>::Fail("Empty shader: " + path.string());
  }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(data.data()), size);
  return Result<std::vector<std::uint8_t>>::Ok(std::move(data));
}

struct Vertex {
  float px, py, pz;
  float r, g, b;
  float u, v;
};

class D3D12Device final : public IDevice {
 public:
  Status Init(const DeviceDesc& desc) {
    if (!desc.native_window || desc.width == 0 || desc.height == 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid DeviceDesc");
    }
    hwnd_ = static_cast<HWND>(desc.native_window);
    width_ = desc.width;
    height_ = desc.height;

#if defined(_DEBUG)
    {
      ComPtr<ID3D12Debug> debug;
      if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
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
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                      nullptr))) {
        break;
      }
      adapter.Reset();
    }
    if (!adapter) {
      if (FAILED(factory->EnumAdapters1(0, &adapter))) {
        return Status::Fail("No DXGI adapter");
      }
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

    if (auto st = CreateSwapchain(); !st) {
      return st;
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

    LogInfo("D3D12 device ready");
    return Status::Ok();
  }

  ~D3D12Device() override {
    WaitGpu();
    if (fence_event_) {
      CloseHandle(fence_event_);
      fence_event_ = nullptr;
    }
  }

  Status BeginFrame() override {
    const auto frame = frame_index_;
    const UINT64 fence_to_wait = fence_values_[frame];
    if (fence_->GetCompletedValue() < fence_to_wait) {
      if (FAILED(fence_->SetEventOnCompletion(fence_to_wait, fence_event_))) {
        return Status::Fail("SetEventOnCompletion failed");
      }
      WaitForSingleObject(fence_event_, INFINITE);
    }

    if (FAILED(allocators_[frame]->Reset())) {
      return Status::Fail("CommandAllocator::Reset failed");
    }
    if (FAILED(command_list_->Reset(allocators_[frame].Get(), nullptr))) {
      return Status::Fail("CommandList::Reset failed");
    }

    auto* backbuffer = backbuffers_[swapchain_->GetCurrentBackBufferIndex()].Get();
    Transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    return Status::Ok();
  }

  Status Clear(const ColorRgba& color) override {
    const auto index = swapchain_->GetCurrentBackBufferIndex();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
                                          static_cast<SIZE_T>(index) * rtv_descriptor_size_};
    const float clear[4] = {color.r, color.g, color.b, color.a};
    command_list_->ClearRenderTargetView(rtv, clear, 0, nullptr);
    if (dsv_) {
      command_list_->ClearDepthStencilView(dsv_heap_->GetCPUDescriptorHandleForHeapStart(),
                                           D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
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

  Status DrawSimpleMesh() override {
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

  Status DispatchCompute(const ComputeDispatchDesc& desc) override {
    if (desc.groups_x == 0 || desc.groups_y == 0 || desc.groups_z == 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "compute groups must be > 0");
    }
    // Real compute PSO arrives later; contract validates and records intent.
    ++compute_dispatches_;
    return Status::Ok();
  }

  Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override {
    w = static_cast<int>(width_);
    h = static_cast<int>(height_);
    out_rgba.assign(static_cast<std::size_t>(w * h * 4), 0);
    // Stub: solid dark blue (matches typical clear) for pipeline smoke.
    for (int i = 0; i < w * h; ++i) {
      out_rgba[static_cast<std::size_t>(i * 4 + 0)] = 13;
      out_rgba[static_cast<std::size_t>(i * 4 + 1)] = 18;
      out_rgba[static_cast<std::size_t>(i * 4 + 2)] = 26;
      out_rgba[static_cast<std::size_t>(i * 4 + 3)] = 255;
    }
    return Status::Ok();
  }

  Status Present() override {
    auto* backbuffer = backbuffers_[swapchain_->GetCurrentBackBufferIndex()].Get();
    Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    if (FAILED(command_list_->Close())) {
      return Status::Fail("CommandList::Close failed");
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    const HRESULT hr = swapchain_->Present(1, 0);
    if (FAILED(hr)) {
      return Status::Fail("Present failed: " + HrToString(hr));
    }

    const UINT64 signal = ++fence_value_;
    if (FAILED(queue_->Signal(fence_.Get(), signal))) {
      return Status::Fail("Queue::Signal failed");
    }
    fence_values_[frame_index_] = signal;
    frame_index_ = (frame_index_ + 1) % kFrameCount;
    return Status::Ok();
  }

  Status Resize(std::uint32_t width, std::uint32_t height) override {
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
    DXGI_SWAP_CHAIN_DESC1 scd{};
    swapchain_->GetDesc1(&scd);
    const HRESULT hr =
        swapchain_->ResizeBuffers(kFrameCount, width_, height_, scd.Format, scd.Flags);
    if (FAILED(hr)) {
      return Status::Fail("ResizeBuffers failed: " + HrToString(hr));
    }
    if (auto st = CreateRenderTargets(); !st) {
      return st;
    }
    return CreateDepthBuffer();
  }

  Status SetupSimpleMesh(const SimpleMeshShaders& shaders) override {
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

 private:
  Status CreateSwapchain() {
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = width_;
    scd.Height = height_;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = kFrameCount;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

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
    return Status::Ok();
  }

  Status CreateFrameResources() {
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

  Status CreateRenderTargets() {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
      HRESULT hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i]));
      if (FAILED(hr)) {
        return Status::Fail("SwapChain::GetBuffer failed");
      }
      device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, handle);
      handle.ptr += rtv_descriptor_size_;
    }
    frame_index_ = 0;
    return Status::Ok();
  }

  Status CreateDepthBuffer() {
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
    desc.Format = DXGI_FORMAT_D32_FLOAT;
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
    device_->CreateDepthStencilView(dsv_.Get(), nullptr,
                                    dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    return Status::Ok();
  }

  Status CreateVertexBuffer() {
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

  Status CreateCheckerTexture() {
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

  void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &barrier);
  }

  void WaitGpu() {
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
    for (auto& v : fence_values_) {
      v = signal;
    }
  }

  HWND hwnd_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool mesh_ready_ = false;
  std::uint32_t compute_dispatches_ = 0;

  ComPtr<IDXGIFactory6> factory_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<IDXGISwapChain3> swapchain_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  ComPtr<ID3D12DescriptorHeap> srv_heap_;
  UINT rtv_descriptor_size_ = 0;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> backbuffers_{};
  std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> allocators_{};
  ComPtr<ID3D12GraphicsCommandList> command_list_;
  ComPtr<ID3D12Resource> dsv_;
  ComPtr<ID3D12Resource> vertex_buffer_;
  ComPtr<ID3D12Resource> texture_;
  ComPtr<ID3D12Resource> texture_upload_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pso_;
  D3D12_VERTEX_BUFFER_VIEW vbv_{};
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  UINT64 fence_value_ = 0;
  std::array<UINT64, kFrameCount> fence_values_{};
  std::uint32_t frame_index_ = 0;
};

}  // namespace

Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc& desc) {
  auto device = std::make_unique<D3D12Device>();
  if (auto st = device->Init(desc); !st) {
    return Result<std::unique_ptr<IDevice>>::Fail(st);
  }
  return Result<std::unique_ptr<IDevice>>::Ok(std::unique_ptr<IDevice>(std::move(device)));
}

}  // namespace engine::rhi
