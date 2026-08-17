#include "engine/rhi/i_device.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace engine::rhi {
namespace {

constexpr std::uint32_t kFrameCount = 2;
constexpr UINT kMaxGpuPasses = 32;
constexpr UINT kMaxTimestampQueries = 64;  // 32 begin/end pairs

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

  [[nodiscard]] bool is_headless() const override { return gpu_headless_; }

  void SetVSync(bool enabled) override { vsync_ = enabled; }
  [[nodiscard]] bool vsync() const override { return vsync_; }

  UINT CurrentBbIndex() const {
    if (gpu_headless_ || !swapchain_) {
      return offscreen_bb_index_;
    }
    return swapchain_->GetCurrentBackBufferIndex();
  }

  ~D3D12Device() override {
    WaitGpu();
    if (fence_event_) {
      CloseHandle(fence_event_);
      fence_event_ = nullptr;
    }
    engine::ClearFeatureOverrides();
  }

  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }

  Status BeginFrame() override {
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

    auto* backbuffer = backbuffers_[CurrentBbIndex()].Get();
    Transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    return Status::Ok();
  }

  Status Clear(const ColorRgba& color) override {
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
    if (cull_pso_ && command_list_) {
      command_list_->SetPipelineState(cull_pso_.Get());
      command_list_->SetComputeRootSignature(cull_root_.Get());
      command_list_->Dispatch(desc.groups_x, desc.groups_y, desc.groups_z);
    }
    ++compute_dispatches_;
    return Status::Ok();
  }

  Status SetupInstanceCullCompute(const std::filesystem::path& cs_dxil) override {
    if (!device_ || cs_dxil.empty()) {
      return Status::Fail("SetupInstanceCullCompute: invalid");
    }
    std::ifstream in(cs_dxil, std::ios::binary);
    if (!in) {
      return Status::Fail("Cull CS missing: " + cs_dxil.string());
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
      return Status::Fail("Cull CS empty");
    }

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 20;
    params[0].Constants.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].Descriptor.ShaderRegister = 1;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 3;
    rs.pParameters = params;
    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
      return Status::Fail("Serialize cull root failed");
    }
    if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                            IID_PPV_ARGS(&cull_root_)))) {
      return Status::Fail("Create cull root failed");
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = cull_root_.Get();
    pso.CS = {bytes.data(), bytes.size()};
    if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull_pso_)))) {
      return Status::Fail("Create cull PSO failed");
    }
    cull_ready_ = true;
    LogInfo("D3D12 instance cull CS ready (UAV IndirectArgs + compact indices)");
    return Status::Ok();
  }

  Status DispatchInstanceCull(const Mat4& view_proj, std::uint32_t instance_count,
                              std::uint32_t& out_visible) override {
    out_visible = instance_count;
    if (!cull_ready_ || !command_list_ || instance_count == 0) {
      return Status::Ok();
    }
    if (!indirect_args_buf_) {
      return Status::Fail("DispatchInstanceCull: UploadIndirectIndexedArgs first");
    }

    // Zero InstanceCount (offset 4) then CS InterlockedAdd per visible thread.
    if (indirect_args_state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
      Transition(indirect_args_buf_.Get(), indirect_args_state_, D3D12_RESOURCE_STATE_COPY_DEST);
      indirect_args_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if (!indirect_zero_upload_) {
      D3D12_HEAP_PROPERTIES upload{};
      upload.Type = D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC buf{};
      buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      buf.Width = sizeof(UINT);
      buf.Height = 1;
      buf.DepthOrArraySize = 1;
      buf.MipLevels = 1;
      buf.SampleDesc.Count = 1;
      buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if (FAILED(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buf,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  IID_PPV_ARGS(&indirect_zero_upload_)))) {
        return Status::Fail("Create indirect zero upload failed");
      }
      void* mapped = nullptr;
      if (FAILED(indirect_zero_upload_->Map(0, nullptr, &mapped)) || !mapped) {
        return Status::Fail("Map indirect zero upload failed");
      }
      const UINT z = 0;
      std::memcpy(mapped, &z, sizeof(z));
      indirect_zero_upload_->Unmap(0, nullptr);
    }
    command_list_->CopyBufferRegion(indirect_args_buf_.Get(), sizeof(UINT),
                                    indirect_zero_upload_.Get(), 0, sizeof(UINT));

    Transition(indirect_args_buf_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    indirect_args_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Compact indices UAV (identity map while visibility is always-true).
    const UINT64 compact_bytes =
        (std::max)(static_cast<UINT64>(instance_count) * sizeof(UINT), static_cast<UINT64>(256));
    if (!cull_compact_buf_ || cull_compact_bytes_ < compact_bytes) {
      WaitGpuSubmitted();
      cull_compact_buf_.Reset();
      D3D12_HEAP_PROPERTIES heap{};
      heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC desc{};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      desc.Width = compact_bytes;
      desc.Height = 1;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.SampleDesc.Count = 1;
      desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                 IID_PPV_ARGS(&cull_compact_buf_)))) {
        return Status::Fail("Create cull compact UAV failed");
      }
      cull_compact_bytes_ = compact_bytes;
      cull_compact_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    } else if (cull_compact_state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
      Transition(cull_compact_buf_.Get(), cull_compact_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      cull_compact_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    struct CullCB {
      float vp[16];
      UINT count;
      UINT pad[3];
    } cb{};
    std::memcpy(cb.vp, view_proj.m.data(), sizeof(cb.vp));
    cb.count = instance_count;
    command_list_->SetComputeRootSignature(cull_root_.Get());
    command_list_->SetPipelineState(cull_pso_.Get());
    command_list_->SetComputeRoot32BitConstants(0, 20, &cb, 0);
    command_list_->SetComputeRootUnorderedAccessView(1, indirect_args_buf_->GetGPUVirtualAddress());
    command_list_->SetComputeRootUnorderedAccessView(2, cull_compact_buf_->GetGPUVirtualAddress());
    const UINT groups = (instance_count + 63u) / 64u;
    command_list_->Dispatch(groups, 1, 1);
    ++compute_dispatches_;

    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = indirect_args_buf_.Get();
    command_list_->ResourceBarrier(1, &uav);
    D3D12_RESOURCE_BARRIER uav2{};
    uav2.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav2.UAV.pResource = cull_compact_buf_.Get();
    command_list_->ResourceBarrier(1, &uav2);
    Transition(indirect_args_buf_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    indirect_args_state_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    engine::SetFeatureOverride("hiz", true);
    engine::SetFeatureOverride("execute_indirect", true);
    engine::SetFeatureOverride("gpu_cull_compact", true);
    return Status::Ok();
  }

  Status ProbeBindlessMinimalPath(std::uint32_t srv_heap_slot) override {
    if (!bindless_capable_) {
      return Status::Fail("ProbeBindlessMinimalPath: bindless SKIP (tier < 2)");
    }
    engine::SetFeatureOverride("bindless", true);
    // 最小采样路径证明：对 CBV_SRV_UAV 堆做按槽偏移的 GPU handle（索引式 SRV）。
    // 不在此写入 command list（可在 Init 阶段安全调用）；热路径 DrawLit 已绑定同一堆。
    // Does NOT enable bindless_hot_path — that stays Feature-gated / default OFF.
    ID3D12DescriptorHeap* heap = shadow_srv_heap_ ? shadow_srv_heap_.Get() : srv_heap_.Get();
    if (!heap) {
      return Status::Ok();  // Feature 已可查询；堆在 lit setup 后出现
    }
    const UINT incr =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<SIZE_T>(srv_heap_slot) * static_cast<SIZE_T>(incr);
    bindless_probe_gpu_ptr_ = gpu.ptr;
    return Status::Ok();
  }

  // Albedo heap index for SM6.6 ResourceDescriptorHeap when bindless_hot_path is on.
  // Matches classic registers: tex_slot==0 → t1 (slot 1), else → t4 (slot 4).
  // Early auto-wave used pad=1 for all draws → tex_slot>0 sampled wrong albedo → golden RMSE.
  // Conditions to apply: bindless_capable_ && QueryFeature("bindless_hot_path") && !gpu_headless_.
  // Otherwise return -1 (classic path).
  float BindlessAlbedoHeapPad(float tex_slot) const {
    if (!bindless_capable_ || gpu_headless_ || !engine::QueryFeature("bindless_hot_path")) {
      return -1.f;
    }
    return tex_slot > 0.5f ? 4.f : 1.f;
  }

  Status SetSubmitConfig(const SubmitConfig& cfg) override {
    if (auto st = ValidateSubmitConfig(cfg); !st) {
      return st;
    }
    submit_cfg_ = cfg;
    return Status::Ok();
  }

  Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override {
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
    Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

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

  Status ReadbackDepthRgbaStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override {
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

  Status Present() override {
    auto* backbuffer = backbuffers_[CurrentBbIndex()].Get();
    Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

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
      return Status::Fail("CommandList::Close failed");
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

  Status SetupLitMesh(const LitMeshShaders& shaders) override {
    WaitGpu();
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
    auto shadow_ps = ReadFileBytes(shaders.shadow_ps_dxil);
    if (!shadow_ps) {
      return shadow_ps.status();
    }

    // Lit root: CBV b0, CBV b1, table t0..t8 + t10 (pixel), root SRV t9 (instances VS).
    // t6 = IBL specular prefilter; t10 = reflection probe (independent resources).
    // SM 6.6 ResourceDescriptorHeap needs Root Signature 1.1 + DIRECTLY_INDEXED.
    // CBV/SRV data is rewritten every frame (UPLOAD + shadow maps) → must NOT use DATA_STATIC.
    // Two ranges so t9 stays a root SRV (no overlap with the descriptor table).
    D3D12_DESCRIPTOR_RANGE1 srv_ranges[2]{};
    srv_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_ranges[0].NumDescriptors = 9;  // t0..t8
    srv_ranges[0].BaseShaderRegister = 0;
    srv_ranges[0].RegisterSpace = 0;
    srv_ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    srv_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    srv_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_ranges[1].NumDescriptors = 1;  // t10 reflection probe
    srv_ranges[1].BaseShaderRegister = 10;
    srv_ranges[1].RegisterSpace = 0;
    srv_ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    srv_ranges[1].OffsetInDescriptorsFromTableStart = 10;

    D3D12_ROOT_PARAMETER1 lit_params[4]{};
    lit_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    lit_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    lit_params[0].Descriptor.ShaderRegister = 0;
    lit_params[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    lit_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    lit_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    lit_params[1].Descriptor.ShaderRegister = 1;
    lit_params[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    lit_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lit_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    lit_params[2].DescriptorTable.NumDescriptorRanges = 2;
    lit_params[2].DescriptorTable.pDescriptorRanges = srv_ranges;
    lit_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    lit_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    lit_params[3].Descriptor.ShaderRegister = 9;
    lit_params[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;

    D3D12_STATIC_SAMPLER_DESC lit_samplers[2]{};
    lit_samplers[0].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    lit_samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    lit_samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    lit_samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    lit_samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    lit_samplers[0].MaxAnisotropy = 1;
    lit_samplers[0].MinLOD = 0.f;
    lit_samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    lit_samplers[0].ShaderRegister = 0;
    lit_samplers[0].RegisterSpace = 0;
    lit_samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    lit_samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    lit_samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    lit_samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    lit_samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    lit_samplers[1].MaxAnisotropy = 1;
    lit_samplers[1].MinLOD = 0.f;
    lit_samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    lit_samplers[1].ShaderRegister = 1;
    lit_samplers[1].RegisterSpace = 0;
    lit_samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC lit_vrs{};
    lit_vrs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    lit_vrs.Desc_1_1.NumParameters = 4;
    lit_vrs.Desc_1_1.pParameters = lit_params;
    lit_vrs.Desc_1_1.NumStaticSamplers = 2;
    lit_vrs.Desc_1_1.pStaticSamplers = lit_samplers;
    lit_vrs.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                             D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&lit_vrs, &sig, &err);
    if (FAILED(hr)) {
      const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
      return Status::Fail(std::string("Lit root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                      IID_PPV_ARGS(&lit_root_));
    if (FAILED(hr)) {
      return Status::Fail("Create lit root signature failed");
    }

    D3D12_INPUT_ELEMENT_DESC lit_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lit_pso{};
    lit_pso.pRootSignature = lit_root_.Get();
    lit_pso.VS = {vs->data(), vs->size()};
    lit_pso.PS = {ps->data(), ps->size()};
    lit_pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    lit_pso.SampleMask = UINT_MAX;
    lit_pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // NONE: matches Vulkan lit and avoids hollow untextured cubes from winding/viewport
    // mismatch. Floor slab mitigations are SV_ClipDistance + PS discard (not backface cull).
    // Ground plane indices are CCW from +Y (see Sandbox slot4 upload).
    lit_pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    lit_pso.RasterizerState.FrontCounterClockwise = TRUE;
    // Keep depth clip on: Perspective is Z∈[0,1]. DepthClip=FALSE clamped near-floor
    // depth to 0 and hid the debug grid under a featureless white plane.
    lit_pso.RasterizerState.DepthClipEnable = TRUE;
    lit_pso.DepthStencilState.DepthEnable = TRUE;
    lit_pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    lit_pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    lit_pso.InputLayout = {lit_layout, 3};
    lit_pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lit_pso.NumRenderTargets = 1;
    // Lit goes to HDR scene color; tonemap resolves to the LDR swapchain.
    lit_pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    lit_pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    lit_pso.SampleDesc.Count = 1;
    hr = device_->CreateGraphicsPipelineState(&lit_pso, IID_PPV_ARGS(&lit_pso_));
    if (FAILED(hr)) {
      return Status::Fail(
          "Create lit PSO failed: " + HrToString(hr) +
          " (often SM6.6 bindless PS vs stale binary/root-sig; rebuild Debug+Release)");
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lit_pso_tr = lit_pso;
    lit_pso_tr.BlendState.AlphaToCoverageEnable = FALSE;
    lit_pso_tr.BlendState.RenderTarget[0].BlendEnable = TRUE;
    lit_pso_tr.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    lit_pso_tr.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    lit_pso_tr.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    lit_pso_tr.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    // Clamp rather than clip: near-plane cuts on alpha-blended glass became a floating
    // white slab that appeared/disappeared with the Transparent profiler pass.
    lit_pso_tr.RasterizerState.DepthClipEnable = FALSE;
    lit_pso_tr.DepthStencilState.DepthEnable = TRUE;
    lit_pso_tr.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    lit_pso_tr.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    hr = device_->CreateGraphicsPipelineState(&lit_pso_tr, IID_PPV_ARGS(&lit_pso_transparent_));
    if (FAILED(hr)) {
      return Status::Fail("Create transparent lit PSO failed: " + HrToString(hr));
    }

    // Shadow root: CBV b0 + CBV b1 only.
    D3D12_ROOT_PARAMETER shadow_params[2]{};
    shadow_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadow_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    shadow_params[0].Descriptor.ShaderRegister = 0;
    shadow_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadow_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    shadow_params[1].Descriptor.ShaderRegister = 1;

    D3D12_ROOT_SIGNATURE_DESC shadow_rs{};
    shadow_rs.NumParameters = 2;
    shadow_rs.pParameters = shadow_params;
    shadow_rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    sig.Reset();
    err.Reset();
    hr = D3D12SerializeRootSignature(&shadow_rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
      const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "";
      return Status::Fail(std::string("Shadow root sig failed: ") + msg);
    }
    hr = device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                      IID_PPV_ARGS(&shadow_root_));
    if (FAILED(hr)) {
      return Status::Fail("Create shadow root signature failed");
    }

    // Depth-only: POSITION from LitVertex stride (pos+normal+uv); no color RT.
    D3D12_INPUT_ELEMENT_DESC shadow_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow_pso{};
    shadow_pso.pRootSignature = shadow_root_.Get();
    shadow_pso.VS = {shadow_vs->data(), shadow_vs->size()};
    // Depth-only with NumRenderTargets=0: omit color PS (asset still required above).
    (void)shadow_ps;
    shadow_pso.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    shadow_pso.SampleMask = UINT_MAX;
    shadow_pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    shadow_pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shadow_pso.RasterizerState.FrontCounterClockwise = TRUE;
    shadow_pso.RasterizerState.DepthClipEnable = TRUE;
    // Integer depth-bias units (D32_FLOAT). Vulkan uses depthBiasConstantFactor=2.5
    // (r-scaled units, not a literal copy of 1500) + same slope=2.0.
    shadow_pso.RasterizerState.DepthBias = 1500;
    shadow_pso.RasterizerState.SlopeScaledDepthBias = 2.0f;
    shadow_pso.RasterizerState.DepthBiasClamp = 0.f;
    shadow_pso.DepthStencilState.DepthEnable = TRUE;
    shadow_pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadow_pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadow_pso.InputLayout = {shadow_layout, 1};
    shadow_pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadow_pso.NumRenderTargets = 0;
    shadow_pso.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadow_pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadow_pso.SampleDesc.Count = 1;
    hr = device_->CreateGraphicsPipelineState(&shadow_pso, IID_PPV_ARGS(&shadow_pso_));
    if (FAILED(hr)) {
      return Status::Fail("Create shadow PSO failed: " + HrToString(hr));
    }

    if (!dsv_) {
      if (auto st = CreateDepthBuffer(); !st) {
        return st;
      }
    }
    if (auto st = CreateShadowMap(); !st) {
      return st;
    }
    if (auto st = CreateLitAlbedoTexture(); !st) {
      return st;
    }
    if (auto st = CreateLitOrmTexture(); !st) {
      return st;
    }
    if (auto st = CreateLitAlbedoTextureSlot1(); !st) {
      return st;
    }
    if (auto st = CreateLitOrmTextureSlot1(); !st) {
      return st;
    }
    if (auto st = EnsureDefaultReflectionCubemap(); !st) {
      return st;
    }
    if (auto st = CreateLocalShadowMap(); !st) {
      return st;
    }
    if (auto st = CreateCubeMesh(); !st) {
      return st;
    }
    if (auto st = CreateLitConstantBuffers(); !st) {
      return st;
    }

    // Optional screen quads (paths may be empty).
    quad_ready_ = false;
    if (!shaders.quad_vs_dxil.empty() && !shaders.quad_ps_dxil.empty()) {
      if (auto st = SetupScreenQuads(shaders.quad_vs_dxil, shaders.quad_ps_dxil); !st) {
        return st;
      }
    }
    debug_ready_ = false;
    if (!shaders.debug_vs_dxil.empty() && !shaders.debug_ps_dxil.empty()) {
      if (auto st = SetupDebugLines(shaders.debug_vs_dxil, shaders.debug_ps_dxil); !st) {
        return st;
      }
    }

    lit_ready_ = true;
    LogInfo("Lit cube mesh + shadow map ready");
    return Status::Ok();
  }

  Status SetFrameLighting(const FrameLighting& lighting) override {
    if (!lit_ready_ || !frame_cb_) {
      return Status::Fail("SetupLitMesh not called");
    }
    struct FrameData {
      float view_proj[16];
      float cascade_vp[4][16];
      float sun_dir[3];
      float sun_intensity;
      float ambient[3];
      float shadow_bias;
      float sun_color[3];
      float specular_power;
      float eye[3];
      float enable_shadow;
      float cascade_splits[4];
      float cam_forward[3];
      float cascade_count;
      float tiles_per_row;
      float enable_ssao;
      float enable_taa;
      float local_count;
      float local_pos_range[16][4];
      float local_color_intensity[16][4];
      float local_spot[16][4];       // xyz=dir, w=cosOuter (-1 = point/omni)
      float local_spot_inner[16];    // cosInner for lights 0..15
      float local_shadow_vp[12][16];
      float enable_local_shadow;
      float local_shadow_bias;
      float local_shadow_count;
      float local_shadow_tiles;
      float prev_view_proj[16];
      float jitter_x;
      float jitter_y;
      float enable_reflection;
      float reflection_intensity;
      float enable_ibl;
      float ibl_intensity;
      float _pad_before_ies[2];  // HLSL: float4 g_local_ies starts 16-byte aligned
      float local_ies[16];  // C03/W7 profile id as float (0=off)
      // Mega-W8 C02: packed Forward+ tile lists (must match lit_cube.hlsl).
      float enable_tiled_lights;
      float tile_grid_w;
      float tile_grid_h;
      float max_lights_per_tile;
      float tile_light_count[32];
      float tile_light_index[256];
    } data{};
    std::memcpy(data.view_proj, lighting.view_proj.m.data(), sizeof(data.view_proj));
    for (int i = 0; i < 4; ++i) {
      std::memcpy(data.cascade_vp[i], lighting.cascade_view_proj[static_cast<std::size_t>(i)].m.data(),
                  sizeof(data.cascade_vp[i]));
    }
    data.sun_dir[0] = lighting.sun_direction.x;
    data.sun_dir[1] = lighting.sun_direction.y;
    data.sun_dir[2] = lighting.sun_direction.z;
    data.sun_intensity = lighting.sun_intensity;
    data.ambient[0] = lighting.ambient.r;
    data.ambient[1] = lighting.ambient.g;
    data.ambient[2] = lighting.ambient.b;
    data.shadow_bias = lighting.shadow_bias;
    data.sun_color[0] = lighting.sun_color.r;
    data.sun_color[1] = lighting.sun_color.g;
    data.sun_color[2] = lighting.sun_color.b;
    data.specular_power = lighting.specular_power;
    data.eye[0] = lighting.eye.x;
    data.eye[1] = lighting.eye.y;
    data.eye[2] = lighting.eye.z;
    data.enable_shadow = lighting.enable_shadows ? 1.f : 0.f;
    for (int i = 0; i < 4; ++i) {
      data.cascade_splits[i] = lighting.cascade_splits[static_cast<std::size_t>(i)];
    }
    data.cam_forward[0] = lighting.camera_forward.x;
    data.cam_forward[1] = lighting.camera_forward.y;
    data.cam_forward[2] = lighting.camera_forward.z;
    data.cascade_count = static_cast<float>(lighting.cascade_count);
    data.tiles_per_row = static_cast<float>(lighting.cascade_tiles_per_row);
    data.enable_ssao = lighting.enable_ssao ? 1.f : 0.f;
    data.enable_taa = lighting.enable_taa ? 1.f : 0.f;
    data.local_count = static_cast<float>(lighting.local_light_count);
    for (int i = 0; i < 16; ++i) {
      data.local_pos_range[i][0] = lighting.local_pos[static_cast<std::size_t>(i)].x;
      data.local_pos_range[i][1] = lighting.local_pos[static_cast<std::size_t>(i)].y;
      data.local_pos_range[i][2] = lighting.local_pos[static_cast<std::size_t>(i)].z;
      data.local_pos_range[i][3] = lighting.local_range[static_cast<std::size_t>(i)];
      data.local_color_intensity[i][0] = lighting.local_color[static_cast<std::size_t>(i)].r;
      data.local_color_intensity[i][1] = lighting.local_color[static_cast<std::size_t>(i)].g;
      data.local_color_intensity[i][2] = lighting.local_color[static_cast<std::size_t>(i)].b;
      data.local_color_intensity[i][3] = lighting.local_intensity[static_cast<std::size_t>(i)];
      data.local_spot[i][0] = lighting.local_spot[static_cast<std::size_t>(i)].x;
      data.local_spot[i][1] = lighting.local_spot[static_cast<std::size_t>(i)].y;
      data.local_spot[i][2] = lighting.local_spot[static_cast<std::size_t>(i)].z;
      data.local_spot[i][3] = lighting.local_spot[static_cast<std::size_t>(i)].w;
      data.local_spot_inner[i] = lighting.local_spot_inner[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < 12; ++i) {
      std::memcpy(data.local_shadow_vp[i],
                  lighting.local_shadow_vps[static_cast<std::size_t>(i)].m.data(),
                  sizeof(data.local_shadow_vp[i]));
    }
    // Compat: FrameLighting::local_shadow_vp remains tile 0.
    std::memcpy(data.local_shadow_vp[0], lighting.local_shadow_vp.m.data(),
                sizeof(data.local_shadow_vp[0]));
    data.enable_local_shadow = lighting.enable_local_shadow ? 1.f : 0.f;
    data.local_shadow_bias = lighting.local_shadow_bias;
    data.local_shadow_count = static_cast<float>(lighting.local_shadow_count);
    data.local_shadow_tiles = static_cast<float>(lighting.local_shadow_tiles_per_row);
    std::memcpy(data.prev_view_proj, lighting.prev_view_proj.m.data(), sizeof(data.prev_view_proj));
    data.jitter_x = lighting.jitter_x;
    data.jitter_y = lighting.jitter_y;
    data.enable_reflection = lighting.enable_reflection_probe ? 1.f : 0.f;
    data.reflection_intensity = lighting.reflection_intensity;
    data.enable_ibl = lighting.enable_ibl ? 1.f : 0.f;
    data.ibl_intensity = lighting.ibl_intensity;
    for (int i = 0; i < 16; ++i) {
      data.local_ies[i] = lighting.local_ies[static_cast<std::size_t>(i)];
    }
    data.enable_tiled_lights = lighting.enable_tiled_lights ? 1.f : 0.f;
    data.tile_grid_w = 8.f;
    data.tile_grid_h = 4.f;
    data.max_lights_per_tile = 8.f;
    for (int i = 0; i < 32; ++i) {
      data.tile_light_count[i] = static_cast<float>(lighting.tile_light_count[static_cast<std::size_t>(i)]);
    }
    for (int i = 0; i < 256; ++i) {
      data.tile_light_index[i] = static_cast<float>(lighting.tile_light_index[static_cast<std::size_t>(i)]);
    }

    void* ptr = nullptr;
    if (FAILED(frame_cb_->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map frame CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + FrameCbOffset(), &data, sizeof(data));
    frame_cb_->Unmap(0, nullptr);

    const Mat4& shadow_vp =
        lighting.cascade_count > 0 ? lighting.cascade_view_proj[0] : lighting.light_view_proj;
    float shadow_frame[16]{};
    std::memcpy(shadow_frame, shadow_vp.m.data(), sizeof(shadow_frame));
    if (FAILED(shadow_frame_cb_->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map shadow frame CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + ShadowVpCbOffset(0), shadow_frame, sizeof(shadow_frame));
    shadow_frame_cb_->Unmap(0, nullptr);

    lighting_ = lighting;
    bound_shadow_slot_ = 0;
    return Status::Ok();
  }

  Status BeginShadowPass() override {
    if (!lit_ready_ || !shadow_map_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (shadow_map_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
      Transition(shadow_map_.Get(), shadow_map_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
      shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE shadow_dsv =
        shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    command_list_->ClearDepthStencilView(shadow_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    command_list_->OMSetRenderTargets(0, nullptr, FALSE, &shadow_dsv);

    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(kShadowMapSize);
    vp.Height = static_cast<float>(kShadowMapSize);
    vp.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &vp);
    D3D12_RECT scissor{0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)};
    command_list_->RSSetScissorRects(1, &scissor);
    shadow_active_ = true;
    local_shadow_active_ = false;
    bound_shadow_slot_ = 0;
    shadow_draws_ = 0;
    return Status::Ok();
  }

  Status BindShadowCascade(int cascade_index) override {
    if (!lit_ready_ || !shadow_active_) {
      return Status::Fail("BeginShadowPass not active");
    }
    if (cascade_index < 0 || cascade_index >= lighting_.cascade_count) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid cascade index");
    }

    float shadow_frame[16]{};
    std::memcpy(shadow_frame,
                lighting_.cascade_view_proj[static_cast<std::size_t>(cascade_index)].m.data(),
                sizeof(shadow_frame));
    void* ptr = nullptr;
    if (FAILED(shadow_frame_cb_->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map shadow frame CB failed");
    }
    // Per-cascade slot: must not overwrite other cascades before GPU executes.
    std::memcpy(static_cast<char*>(ptr) + ShadowVpCbOffset(cascade_index), shadow_frame,
                sizeof(shadow_frame));
    shadow_frame_cb_->Unmap(0, nullptr);

    const int tiles_per_row = (std::max)(1, lighting_.cascade_tiles_per_row);
    const float tile = static_cast<float>(kShadowMapSize) / static_cast<float>(tiles_per_row);
    const int ix = cascade_index % tiles_per_row;
    const int iy = cascade_index / tiles_per_row;

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(ix) * tile;
    vp.TopLeftY = static_cast<float>(iy) * tile;
    vp.Width = tile;
    vp.Height = tile;
    vp.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &vp);

    D3D12_RECT scissor{};
    scissor.left = static_cast<LONG>(vp.TopLeftX);
    scissor.top = static_cast<LONG>(vp.TopLeftY);
    scissor.right = scissor.left + static_cast<LONG>(tile);
    scissor.bottom = scissor.top + static_cast<LONG>(tile);
    command_list_->RSSetScissorRects(1, &scissor);

    bound_shadow_slot_ = cascade_index;
    return Status::Ok();
  }

  Status DrawShadowCubes(std::span<const LitDrawItem> items) override {
    if (!lit_ready_ || (!shadow_active_ && !local_shadow_active_)) {
      return Status::Fail("BeginShadowPass/BeginLocalShadowPass not active");
    }
    if (items.empty()) {
      return Status::Ok();
    }

    command_list_->SetPipelineState(shadow_pso_.Get());
    command_list_->SetGraphicsRootSignature(shadow_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
        0, shadow_frame_cb_->GetGPUVirtualAddress() + ShadowVpCbOffset(bound_shadow_slot_));
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (std::size_t i = 0; i < items.size(); ++i) {
      const int slot = items[i].mesh_slot;
      if (slot < 0 || slot >= kMaxMeshSlots || mesh_slots_[slot].index_count == 0) {
        continue;
      }
      command_list_->IASetVertexBuffers(0, 1, &mesh_slots_[slot].vbv);
      command_list_->IASetIndexBuffer(&mesh_slots_[slot].ibv);

      float world[16]{};
      std::memcpy(world, items[i].world.m.data(), sizeof(world));
      const auto offset = ObjectCbOffset(shadow_draws_);
      void* ptr = nullptr;
      if (FAILED(object_cb_->Map(0, nullptr, &ptr))) {
        return Status::Fail("Map object CB failed");
      }
      std::memcpy(static_cast<char*>(ptr) + offset, world, sizeof(world));
      object_cb_->Unmap(0, nullptr);

      command_list_->SetGraphicsRootConstantBufferView(
          1, object_cb_->GetGPUVirtualAddress() + offset);
      command_list_->DrawIndexedInstanced(mesh_slots_[slot].index_count, 1, 0, 0, 0);
      ++shadow_draws_;
    }
    return Status::Ok();
  }

  void BindSceneColorTargets() {
    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &vp);
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    command_list_->RSSetScissorRects(1, &scissor);

    // After ResolvePostEffects, scene_color is still bound as SRV in post_srv_heap for this
    // command list. Rebinding it as RT caused DEVICE_REMOVED (0x887A0005) on Present.
    // Late draws (transparent / Sandbox scale instancing) go to the LDR backbuffer instead.
    if (post_resolved_this_frame_ || !scene_color_ || !hdr_rtv_heap_) {
      const auto index = CurrentBbIndex();
      const D3D12_CPU_DESCRIPTOR_HANDLE rtv{
          rtv_heap_->GetCPUDescriptorHandleForHeapStart().ptr +
          static_cast<SIZE_T>(index) * rtv_descriptor_size_};
      if (dsv_) {
        if (depth_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
          Transition(dsv_.Get(), depth_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
          depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
      } else {
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
      }
      return;
    }

    if (scene_color_state_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
      Transition(scene_color_.Get(), scene_color_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
      scene_color_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE hdr_rtv =
        hdr_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    if (dsv_) {
      if (depth_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        Transition(dsv_.Get(), depth_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depth_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      }
      const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
      command_list_->OMSetRenderTargets(1, &hdr_rtv, FALSE, &dsv);
    } else {
      command_list_->OMSetRenderTargets(1, &hdr_rtv, FALSE, nullptr);
    }
  }

  Status EndShadowPass() override {
    if (!shadow_active_) {
      return Status::Fail("BeginShadowPass not active");
    }
    Transition(shadow_map_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    BindSceneColorTargets();
    shadow_active_ = false;
    return Status::Ok();
  }

  Status BeginLocalShadowPass() override {
    if (!lit_ready_ || !local_shadow_map_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (local_shadow_map_state_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
      Transition(local_shadow_map_.Get(), local_shadow_map_state_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
      local_shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE local_dsv =
        local_shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    // Clear whole atlas; BindLocalShadowTile sets per-tile viewport.
    command_list_->ClearDepthStencilView(local_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    command_list_->OMSetRenderTargets(0, nullptr, FALSE, &local_dsv);

    local_shadow_active_ = true;
    shadow_active_ = false;
    bound_shadow_slot_ = 4;
    // Default to tile 0 for callers that skip BindLocalShadowTile.
    return BindLocalShadowTile(0);
  }

  Status BindLocalShadowTile(int tile_index) override {
    if (!lit_ready_ || !local_shadow_active_) {
      return Status::Fail("BeginLocalShadowPass not active");
    }
    const int count = (std::max)(1, lighting_.local_shadow_tile_count > 0
                                        ? lighting_.local_shadow_tile_count
                                        : lighting_.local_shadow_count);
    if (tile_index < 0 || tile_index >= count) {
      return Status::Fail(ErrorCode::InvalidArgument, "Invalid local shadow tile index");
    }

    float shadow_frame[16]{};
    std::memcpy(shadow_frame,
                lighting_.local_shadow_vps[static_cast<std::size_t>(tile_index)].m.data(),
                sizeof(shadow_frame));
    if (tile_index == 0) {
      // Compat: scheduler may still write only local_shadow_vp.
      std::memcpy(shadow_frame, lighting_.local_shadow_vp.m.data(), sizeof(shadow_frame));
    }
    const int vp_slot = 4 + tile_index;  // after CSM cascade slots 0..3
    void* ptr = nullptr;
    if (FAILED(shadow_frame_cb_->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map shadow frame CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + ShadowVpCbOffset(vp_slot), shadow_frame,
                sizeof(shadow_frame));
    shadow_frame_cb_->Unmap(0, nullptr);

    const int tiles_per_row = (std::max)(1, lighting_.local_shadow_tiles_per_row);
    const float tile = static_cast<float>(kLocalShadowMapSize) / static_cast<float>(tiles_per_row);
    const int ix = tile_index % tiles_per_row;
    const int iy = tile_index / tiles_per_row;

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(ix) * tile;
    vp.TopLeftY = static_cast<float>(iy) * tile;
    vp.Width = tile;
    vp.Height = tile;
    vp.MaxDepth = 1.f;
    command_list_->RSSetViewports(1, &vp);

    D3D12_RECT scissor{};
    scissor.left = static_cast<LONG>(vp.TopLeftX);
    scissor.top = static_cast<LONG>(vp.TopLeftY);
    scissor.right = scissor.left + static_cast<LONG>(tile);
    scissor.bottom = scissor.top + static_cast<LONG>(tile);
    command_list_->RSSetScissorRects(1, &scissor);
    bound_shadow_slot_ = vp_slot;
    return Status::Ok();
  }

  Status EndLocalShadowPass() override {
    if (!local_shadow_active_) {
      return Status::Fail("BeginLocalShadowPass not active");
    }
    Transition(local_shadow_map_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    local_shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    BindSceneColorTargets();
    local_shadow_active_ = false;
    return Status::Ok();
  }

  Status DrawLitCube(const LitDrawItem& item) override {
    return DrawLitCubes(std::span<const LitDrawItem>(&item, 1));
  }

  Status DrawLitCubes(std::span<const LitDrawItem> items) override {
    return DrawLitCubesWithPso(items, lit_pso_.Get());
  }

  Status DrawTransparentLitCubes(std::span<const LitDrawItem> items) override {
    if (!lit_pso_transparent_) {
      return Status::Fail("Transparent lit PSO not ready");
    }
    return DrawLitCubesWithPso(items, lit_pso_transparent_.Get());
  }

  void GpuPassBegin(const char* name) override {
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

  void GpuPassEnd() override {
    if (!timestamp_heap_ || !command_list_) {
      return;
    }
    if (timestamp_cursor_ >= kMaxTimestampQueries) {
      return;
    }
    command_list_->EndQuery(timestamp_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestamp_cursor_);
    ++timestamp_cursor_;
  }

  [[nodiscard]] bool GpuTimestampAvailable() const override {
    return timestamp_heap_ != nullptr && timestamp_freq_ > 0;
  }

  [[nodiscard]] std::vector<GpuPassTiming> LastGpuPassTimings() const override {
    return last_gpu_timings_;
  }

  Status DrawLitCubesWithPso(std::span<const LitDrawItem> items, ID3D12PipelineState* pso) {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (items.empty()) {
      return Status::Ok();
    }
    BindSceneColorTargets();
    if (shadow_map_ && shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      Transition(shadow_map_.Get(), shadow_map_state_,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (local_shadow_map_ &&
        local_shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      Transition(local_shadow_map_.Get(), local_shadow_map_state_,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      local_shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    command_list_->SetPipelineState(pso);
    command_list_->SetGraphicsRootSignature(lit_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
        0, frame_cb_->GetGPUVirtualAddress() + FrameCbOffset());
    ID3D12DescriptorHeap* heaps[] = {shadow_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootDescriptorTable(
        2, shadow_srv_heap_->GetGPUDescriptorHandleForHeapStart());
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

    if (auto* ib = CurrentInstanceBuf()) {
      command_list_->SetGraphicsRootShaderResourceView(3, ib->GetGPUVirtualAddress());
    }

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
      // Default classic (pad=-1). Feature bindless_hot_path + capable + !headless →
      // ResourceDescriptorHeap[tex_slot?4:1]. Hardcoded pad=1 previously drifted golden.
      od.pad = BindlessAlbedoHeapPad(od.tex_slot);

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
    lit_draws_ += static_cast<std::uint32_t>(items.size());
    return Status::Ok();
  }

  Status UploadInstanceTransforms(std::span<const Mat4> worlds) override {
    instance_worlds_.assign(worlds.begin(), worlds.end());
    if (worlds.empty() || !device_) {
      return Status::Ok();
    }
    const UINT64 bytes = static_cast<UINT64>(worlds.size() * sizeof(Mat4));
    auto& buf = instance_bufs_[frame_index_];
    auto& buf_bytes = instance_buf_bytes_[frame_index_];
    // This frame's allocator is idle (BeginFrame waited) — safe to recreate this slot.
    if (!buf || buf_bytes < bytes) {
      buf.Reset();
      D3D12_HEAP_PROPERTIES upload{};
      upload.Type = D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC desc{};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      desc.Width = (std::max)(bytes, static_cast<UINT64>(1024 * sizeof(Mat4)));
      desc.Height = 1;
      desc.DepthOrArraySize = 1;
      desc.MipLevels = 1;
      desc.SampleDesc.Count = 1;
      desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      const HRESULT hr =
          device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &desc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                           IID_PPV_ARGS(&buf));
      if (FAILED(hr)) {
        return Status::Fail("Create instance buffer failed: " + HrToString(hr));
      }
      buf_bytes = desc.Width;
    }
    void* ptr = nullptr;
    if (FAILED(buf->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map instance buffer failed");
    }
    std::memcpy(ptr, worlds.data(), static_cast<std::size_t>(bytes));
    buf->Unmap(0, nullptr);
    engine::SetFeatureOverride("gpu_instancing", true);
    return Status::Ok();
  }

  ID3D12Resource* CurrentInstanceBuf() const {
    return instance_bufs_[frame_index_].Get();
  }

  Status DrawLitInstanced(const LitDrawItem& prototype, std::uint32_t instance_count) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (instance_count == 0) {
      return Status::Ok();
    }
    auto* ib = CurrentInstanceBuf();
    if (!ib || instance_worlds_.size() < instance_count) {
      return IDevice::DrawLitInstanced(prototype, instance_count);
    }
    BindSceneColorTargets();
    if (shadow_map_ && shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      Transition(shadow_map_.Get(), shadow_map_state_,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (local_shadow_map_ &&
        local_shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      Transition(local_shadow_map_.Get(), local_shadow_map_state_,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      local_shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    command_list_->SetPipelineState(lit_pso_.Get());
    command_list_->SetGraphicsRootSignature(lit_root_.Get());
    command_list_->SetGraphicsRootConstantBufferView(
        0, frame_cb_->GetGPUVirtualAddress() + FrameCbOffset());
    ID3D12DescriptorHeap* heaps[] = {shadow_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootDescriptorTable(
        2, shadow_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->SetGraphicsRootShaderResourceView(3, ib->GetGPUVirtualAddress());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const int slot = prototype.mesh_slot;
    if (slot < 0 || slot >= kMaxMeshSlots || mesh_slots_[slot].index_count == 0) {
      return Status::Fail("Invalid mesh for instancing");
    }
    command_list_->IASetVertexBuffers(0, 1, &mesh_slots_[slot].vbv);
    command_list_->IASetIndexBuffer(&mesh_slots_[slot].ibv);

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
    } od{};
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
    // Instanced path stays classic; bindless_hot_path is opaque DrawLit only.
    od.pad = -1.f;

    const auto offset = ObjectCbOffset(kMaxLitDraws - 1);  // dedicated late/instanced slot
    void* ptr = nullptr;
    if (FAILED(object_cb_->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map object CB failed");
    }
    std::memcpy(static_cast<char*>(ptr) + offset, &od, sizeof(od));
    object_cb_->Unmap(0, nullptr);
    command_list_->SetGraphicsRootConstantBufferView(
        1, object_cb_->GetGPUVirtualAddress() + offset);
    // Prefer CPU instance count. ExecuteIndirect + Cull CS can disagree with the
    // uploaded transform buffer and shear the green scale pillars on D3D12.
    command_list_->DrawIndexedInstanced(mesh_slots_[slot].index_count, instance_count, 0, 0, 0);
    lit_draws_ += instance_count;
    return Status::Ok();
  }

  Status EnsureProbeFaceTargets(int face_size) {
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

  static Mat4 MakeProbeFaceVp(const Vec3& probe_pos, int face) {
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

  Status CaptureReflectionProbeGpu(const Vec3& probe_pos, int face_size,
                                   std::span<const LitDrawItem> items) override {
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

  Status UploadIndirectIndexedArgs(std::span<const std::uint32_t> raw_u32) override {
    if (raw_u32.empty() || (raw_u32.size() % 5) != 0 || !device_) {
      return Status::Fail("Invalid indirect args");
    }
    const UINT64 bytes = static_cast<UINT64>(raw_u32.size() * sizeof(std::uint32_t));
    if (!indirect_args_buf_ || indirect_args_bytes_ < bytes) {
      WaitGpuSubmitted();
      indirect_args_buf_.Reset();
      D3D12_HEAP_PROPERTIES def{};
      def.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC buf{};
      buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      buf.Width = bytes;
      buf.Height = 1;
      buf.DepthOrArraySize = 1;
      buf.MipLevels = 1;
      buf.SampleDesc.Count = 1;
      buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      buf.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      const HRESULT hr =
          device_->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &buf,
                                           D3D12_RESOURCE_STATE_COMMON, nullptr,
                                           IID_PPV_ARGS(&indirect_args_buf_));
      if (FAILED(hr)) {
        return Status::Fail("Create indirect args UAV failed");
      }
      indirect_args_bytes_ = bytes;
      indirect_args_state_ = D3D12_RESOURCE_STATE_COMMON;
    }

    if (!indirect_args_upload_[frame_index_] ||
        indirect_args_upload_bytes_[frame_index_] < bytes) {
      // This frame slot is idle after BeginFrame wait.
      indirect_args_upload_[frame_index_].Reset();
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
      if (FAILED(device_->CreateCommittedResource(
              &upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_PPV_ARGS(&indirect_args_upload_[frame_index_])))) {
        return Status::Fail("Create indirect args upload failed");
      }
      indirect_args_upload_bytes_[frame_index_] = bytes;
    }

    void* ptr = nullptr;
    if (FAILED(indirect_args_upload_[frame_index_]->Map(0, nullptr, &ptr))) {
      return Status::Fail("Map indirect args upload failed");
    }
    // Seed index_count etc.; instance_count may be overwritten by Cull CS.
    std::memcpy(ptr, raw_u32.data(), static_cast<std::size_t>(bytes));
    indirect_args_upload_[frame_index_]->Unmap(0, nullptr);

    if (command_list_) {
      if (indirect_args_state_ != D3D12_RESOURCE_STATE_COPY_DEST) {
        Transition(indirect_args_buf_.Get(), indirect_args_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        indirect_args_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
      }
      command_list_->CopyBufferRegion(indirect_args_buf_.Get(), 0,
                                      indirect_args_upload_[frame_index_].Get(), 0, bytes);
      Transition(indirect_args_buf_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
      indirect_args_state_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    if (!draw_indexed_cmd_sig_) {
      D3D12_INDIRECT_ARGUMENT_DESC arg{};
      arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
      D3D12_COMMAND_SIGNATURE_DESC sig{};
      sig.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
      sig.NumArgumentDescs = 1;
      sig.pArgumentDescs = &arg;
      const HRESULT hr =
          device_->CreateCommandSignature(&sig, nullptr, IID_PPV_ARGS(&draw_indexed_cmd_sig_));
      if (FAILED(hr)) {
        return Status::Fail("CreateCommandSignature failed");
      }
    }
    engine::SetFeatureOverride("execute_indirect", true);
    return Status::Ok();
  }

  Status ExecuteIndirectIndexed(std::uint32_t draw_count) override {
    if (!draw_indexed_cmd_sig_ || !indirect_args_buf_ || draw_count == 0) {
      return Status::Fail("ExecuteIndirect not ready");
    }
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    BindSceneColorTargets();
    if (shadow_map_ && shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      Transition(shadow_map_.Get(), shadow_map_state_,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if (local_shadow_map_ &&
        local_shadow_map_state_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      Transition(local_shadow_map_.Get(), local_shadow_map_state_,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      local_shadow_map_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
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
    if (mesh_slots_[0].index_count == 0) {
      return Status::Fail("mesh slot 0 empty for ExecuteIndirect");
    }
    command_list_->IASetVertexBuffers(0, 1, &mesh_slots_[0].vbv);
    command_list_->IASetIndexBuffer(&mesh_slots_[0].ibv);
    command_list_->SetGraphicsRootConstantBufferView(
        1, object_cb_->GetGPUVirtualAddress() + ObjectCbOffset(kMaxLitDraws - 1));
    if (indirect_args_state_ != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
      Transition(indirect_args_buf_.Get(), indirect_args_state_,
                 D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
      indirect_args_state_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    command_list_->ExecuteIndirect(draw_indexed_cmd_sig_.Get(), draw_count,
                                   indirect_args_buf_.Get(), 0, nullptr, 0);
    lit_draws_ += draw_count;
    return Status::Ok();
  }

  Status SetupPostMesh(const PostShaders& shaders) override {
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

  Status ResolvePostEffects(const PostResolveDesc& desc) override {
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
      float pad_c04_a;
      float pad_c04_b;
    } cb{};
    static_assert(sizeof(PostCB) <= 512, "post CB exceeds upload buffer");
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

  Status DrawScreenQuads(std::span<const ScreenQuad> quads) override {
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

  Status DrawDebugLines(std::span<const DebugLineVertex> lines_as_segments) override {
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

  Status SetupUiMesh(const SimpleMeshShaders& shaders) override {
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

  Status UploadUiFontAtlas(const std::uint8_t* rgba, int width, int height) override {
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

  Status DrawUiMesh(std::span<const UiVertex> vertices, std::span<const std::uint16_t> indices,
                    std::span<const UiDrawCmd> commands) override {
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

 private:
  static constexpr UINT kMaxLitDraws = 64;
  static constexpr UINT kShadowVpSlots = 16;  // 4 cascades + local tiles
  static constexpr UINT64 kFrameCbBytes = 8192;  // ≥ FrameData (+ tile lists), 256-aligned
  static constexpr UINT64 kPostCbBytes = 512;

  UINT64 FrameCbOffset() const { return static_cast<UINT64>(frame_index_) * kFrameCbBytes; }
  UINT64 PostCbOffset() const { return static_cast<UINT64>(frame_index_) * kPostCbBytes; }
  UINT64 SkyCbOffset() const { return static_cast<UINT64>(frame_index_) * 256ull; }
  UINT64 ObjectCbOffset(std::size_t draw_index) const {
    return (static_cast<UINT64>(frame_index_) * kMaxLitDraws +
            static_cast<UINT64>(draw_index % kMaxLitDraws)) *
           256ull;
  }
  UINT64 ShadowVpCbOffset(int slot) const {
    const int s = (std::max)(0, (std::min)(slot, static_cast<int>(kShadowVpSlots) - 1));
    return (static_cast<UINT64>(frame_index_) * kShadowVpSlots + static_cast<UINT64>(s)) * 256ull;
  }
  static constexpr int kMaxMeshSlots = 8;
  static constexpr UINT kShadowMapSize = 2048;
  static constexpr UINT kLocalShadowMapSize = 2048;

  struct MeshSlotGpu {
    ComPtr<ID3D12Resource> vb;
    ComPtr<ID3D12Resource> ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT index_count = 0;
  };

  Status CreateShadowMap() {
    shadow_map_.Reset();
    shadow_dsv_heap_.Reset();
    shadow_srv_heap_.Reset();

    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap{};
    dsv_heap.NumDescriptors = 1;
    dsv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    HRESULT hr = device_->CreateDescriptorHeap(&dsv_heap, IID_PPV_ARGS(&shadow_dsv_heap_));
    if (FAILED(hr)) {
      return Status::Fail("Create shadow DSV heap failed");
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap{};
    srv_heap.NumDescriptors = 16;  // t0..t8 + t10 probe + bindless-friendly room
    srv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srv_heap, IID_PPV_ARGS(&shadow_srv_heap_));
    if (FAILED(hr)) {
      return Status::Fail("Create shadow SRV heap failed");
    }
    cbv_srv_uav_descriptor_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kShadowMapSize;
    desc.Height = kShadowMapSize;
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
                                          IID_PPV_ARGS(&shadow_map_));
    if (FAILED(hr)) {
      return Status::Fail("Create shadow map failed: " + HrToString(hr));
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(shadow_map_.Get(), &dsv,
                                    shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(shadow_map_.Get(), &srv,
                                      shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart());

    shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    lit_albedo_.Reset();
    lit_orm_.Reset();
    lit_albedo2_.Reset();
    lit_orm2_.Reset();
    local_shadow_map_.Reset();
    local_shadow_dsv_heap_.Reset();
    return Status::Ok();
  }

  Status CreateLocalShadowMap() {
    local_shadow_map_.Reset();
    local_shadow_dsv_heap_.Reset();

    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap{};
    dsv_heap.NumDescriptors = 1;
    dsv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    HRESULT hr = device_->CreateDescriptorHeap(&dsv_heap, IID_PPV_ARGS(&local_shadow_dsv_heap_));
    if (FAILED(hr)) {
      return Status::Fail("Create local shadow DSV heap failed");
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kLocalShadowMapSize;
    desc.Height = kLocalShadowMapSize;
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
                                          IID_PPV_ARGS(&local_shadow_map_));
    if (FAILED(hr)) {
      return Status::Fail("Create local shadow map failed: " + HrToString(hr));
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(local_shadow_map_.Get(), &dsv,
                                    local_shadow_dsv_heap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE local_srv = shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    local_srv.ptr += static_cast<SIZE_T>(2) * cbv_srv_uav_descriptor_size_;
    device_->CreateShaderResourceView(local_shadow_map_.Get(), &srv, local_srv);

    local_shadow_map_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    return Status::Ok();
  }

  Status UploadRgbaTexture(ComPtr<ID3D12Resource>& tex, UINT srv_slot, const std::uint8_t* rgba,
                           int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
      return Status::Fail("Invalid RGBA texture upload");
    }
    if (!shadow_srv_heap_) {
      return Status::Fail("Shadow SRV heap missing");
    }
    tex.Reset();

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
                                                  IID_PPV_ARGS(&tex));
    if (FAILED(hr)) {
      return Status::Fail("Create RGBA texture failed");
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
      return Status::Fail("Create RGBA upload failed");
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
    dst_loc.pResource = tex.Get();
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource = upload.Get();
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = layout;
    command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

    Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list_->Close();
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    WaitGpu();

    D3D12_CPU_DESCRIPTOR_HANDLE srv = shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    srv.ptr += static_cast<SIZE_T>(srv_slot) * cbv_srv_uav_descriptor_size_;
    device_->CreateShaderResourceView(tex.Get(), nullptr, srv);
    return Status::Ok();
  }

  Status CreateLitAlbedoTexture() {
    constexpr UINT w = 64;
    constexpr UINT h = 64;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4));
    for (UINT y = 0; y < h; ++y) {
      for (UINT x = 0; x < w; ++x) {
        const bool light = ((x / 8) + (y / 8)) % 2 == 0;
        const std::uint8_t c = light ? 240 : 48;
        const auto i = static_cast<std::size_t>((y * w + x) * 4);
        pixels[i + 0] = c;
        pixels[i + 1] = c;
        pixels[i + 2] = c;
        pixels[i + 3] = 255;
      }
    }
    return UploadRgbaTexture(lit_albedo_, 1, pixels.data(), static_cast<int>(w),
                             static_cast<int>(h));
  }

  Status CreateLitOrmTexture() {
    // Neutral ORM: AO=1, roughness=0.5, metallic=0
    constexpr UINT w = 4;
    constexpr UINT h = 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
      pixels[i + 0] = 255;
      pixels[i + 1] = 128;
      pixels[i + 2] = 0;
      pixels[i + 3] = 255;
    }
    return UploadRgbaTexture(lit_orm_, 3, pixels.data(), static_cast<int>(w), static_cast<int>(h));
  }

  Status CreateLitAlbedoTextureSlot1() {
    constexpr UINT w = 4;
    constexpr UINT h = 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4), 255);
    return UploadRgbaTexture(lit_albedo2_, 4, pixels.data(), static_cast<int>(w),
                             static_cast<int>(h));
  }

  Status CreateLitOrmTextureSlot1() {
    constexpr UINT w = 4;
    constexpr UINT h = 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
      pixels[i + 0] = 255;
      pixels[i + 1] = 128;
      pixels[i + 2] = 0;
      pixels[i + 3] = 255;
    }
    return UploadRgbaTexture(lit_orm2_, 5, pixels.data(), static_cast<int>(w), static_cast<int>(h));
  }

  Status BindReflectionCubeSrv() {
    return BindCubeSrv(10, reflection_cube_.Get());
  }

  Status EnsureDefaultReflectionCubemap() {
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

  Status UploadReflectionCubemap(const std::uint8_t* rgba_faces, int face_size) override {
    if (auto st = UploadCubemapResource(reflection_cube_, rgba_faces, face_size); !st) {
      return st;
    }
    reflection_cube_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    probe_cube_size_ = face_size;
    return BindCubeSrv(10, reflection_cube_.Get());
  }

  Status BindCubeSrv(UINT slot, ID3D12Resource* cube) {
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

  Status UploadIblIrradianceCubemap(const std::uint8_t* rgba_faces, int face_size) override {
    // Dedicated irradiance cube at t7.
    if (auto st = UploadCubemapResource(ibl_irradiance_, rgba_faces, face_size); !st) {
      return st;
    }
    return BindCubeSrv(7, ibl_irradiance_.Get());
  }

  Status UploadIblPrefilterCubemap(const std::uint8_t* rgba_faces, int face_size) override {
    // Dedicated specular prefilter at t6 (independent of reflection probe t10).
    if (auto st = UploadCubemapResource(ibl_prefilter_, rgba_faces, face_size); !st) {
      return st;
    }
    return BindCubeSrv(6, ibl_prefilter_.Get());
  }

  Status UploadIblBrdfLut(const std::uint8_t* rgba, int w, int h) override {
    if (!rgba || w <= 0 || h <= 0) {
      return Status::Fail("Invalid BRDF LUT");
    }
    return UploadRgbaTexture(ibl_brdf_lut_, 8, rgba, w, h);
  }

  Status SetupSkybox(const std::filesystem::path& vs_dxil,
                     const std::filesystem::path& ps_dxil) override {
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

  Status UploadSkyCubemap(const std::uint8_t* rgba_faces, int face_size) override {
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

  Status DrawSkybox(const Mat4& view_rot_proj) override {
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

  Status UploadCubemapResource(ComPtr<ID3D12Resource>& cube, const std::uint8_t* rgba_faces,
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

  Status UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height, int slot) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (slot == 0) {
      return UploadRgbaTexture(lit_albedo_, 1, rgba, width, height);
    }
    if (slot == 1) {
      return UploadRgbaTexture(lit_albedo2_, 4, rgba, width, height);
    }
    return Status::Fail("Invalid albedo slot");
  }

  Status UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height, int slot) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (slot == 0) {
      return UploadRgbaTexture(lit_orm_, 3, rgba, width, height);
    }
    if (slot == 1) {
      return UploadRgbaTexture(lit_orm2_, 5, rgba, width, height);
    }
    return Status::Fail("Invalid ORM slot");
  }

  Status SetupScreenQuads(const std::filesystem::path& vs_path,
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

  Status SetupDebugLines(const std::filesystem::path& vs_path,
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

  Status CreateCubeMesh() {
    const LitVertex verts[] = {
        // +Z
        {-0.5f, -0.5f, 0.5f, 0, 0, 1, 0, 0},  {0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 0},
        {0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 1},    {-0.5f, 0.5f, 0.5f, 0, 0, 1, 0, 1},
        // -Z
        {0.5f, -0.5f, -0.5f, 0, 0, -1, 0, 0}, {-0.5f, -0.5f, -0.5f, 0, 0, -1, 1, 0},
        {-0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 1}, {0.5f, 0.5f, -0.5f, 0, 0, -1, 0, 1},
        // +X
        {0.5f, -0.5f, 0.5f, 1, 0, 0, 0, 0},   {0.5f, -0.5f, -0.5f, 1, 0, 0, 1, 0},
        {0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 1},   {0.5f, 0.5f, 0.5f, 1, 0, 0, 0, 1},
        // -X
        {-0.5f, -0.5f, -0.5f, -1, 0, 0, 0, 0},{-0.5f, -0.5f, 0.5f, -1, 0, 0, 1, 0},
        {-0.5f, 0.5f, 0.5f, -1, 0, 0, 1, 1},  {-0.5f, 0.5f, -0.5f, -1, 0, 0, 0, 1},
        // +Y
        {-0.5f, 0.5f, 0.5f, 0, 1, 0, 0, 0},   {0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 0},
        {0.5f, 0.5f, -0.5f, 0, 1, 0, 1, 1},   {-0.5f, 0.5f, -0.5f, 0, 1, 0, 0, 1},
        // -Y
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

  Status UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                           std::span<const std::uint32_t> indices) override {
    if (mesh_slot < 0 || mesh_slot >= kMaxMeshSlots) {
      return Status::Fail("Invalid mesh slot");
    }
    if (vertices.empty() || indices.empty()) {
      return Status::Fail("Empty lit geometry");
    }
    if (!device_) {
      return Status::Fail("Device not ready");
    }

    MeshSlotGpu& slot = mesh_slots_[mesh_slot];
    // Previous frames may still reference these upload buffers on the GPU.
    if (slot.vb || slot.ib) {
      WaitGpuSubmitted();
    }

    auto create_upload = [&](const void* data, UINT size, ComPtr<ID3D12Resource>& out) -> Status {
      out.Reset();
      D3D12_HEAP_PROPERTIES upload{};
      upload.Type = D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC buf{};
      buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      buf.Width = size;
      buf.Height = 1;
      buf.DepthOrArraySize = 1;
      buf.MipLevels = 1;
      buf.SampleDesc.Count = 1;
      buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      const HRESULT hr = device_->CreateCommittedResource(
          &upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&out));
      if (FAILED(hr)) {
        return Status::Fail("Create lit mesh buffer failed");
      }
      void* mapped = nullptr;
      out->Map(0, nullptr, &mapped);
      std::memcpy(mapped, data, size);
      out->Unmap(0, nullptr);
      return Status::Ok();
    };

    const UINT vb_size = static_cast<UINT>(vertices.size() * sizeof(LitVertex));
    const UINT ib_size = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    if (auto st = create_upload(vertices.data(), vb_size, slot.vb); !st) {
      return st;
    }
    if (auto st = create_upload(indices.data(), ib_size, slot.ib); !st) {
      return st;
    }
    slot.vbv.BufferLocation = slot.vb->GetGPUVirtualAddress();
    slot.vbv.SizeInBytes = vb_size;
    slot.vbv.StrideInBytes = sizeof(LitVertex);
    slot.ibv.BufferLocation = slot.ib->GetGPUVirtualAddress();
    slot.ibv.SizeInBytes = ib_size;
    slot.ibv.Format = DXGI_FORMAT_R32_UINT;
    slot.index_count = static_cast<UINT>(indices.size());
    return Status::Ok();
  }

  Status CreateLitConstantBuffers() {
    auto make_cb = [&](UINT64 size, ComPtr<ID3D12Resource>& out) -> Status {
      D3D12_HEAP_PROPERTIES upload{};
      upload.Type = D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC buf{};
      buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      buf.Width = size;
      buf.Height = 1;
      buf.DepthOrArraySize = 1;
      buf.MipLevels = 1;
      buf.SampleDesc.Count = 1;
      buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      const HRESULT hr = device_->CreateCommittedResource(
          &upload, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&out));
      if (FAILED(hr)) {
        return Status::Fail("Create CB failed");
      }
      return Status::Ok();
    };
    if (auto st = make_cb(kFrameCbBytes * kFrameCount, frame_cb_); !st) {
      return st;
    }
    if (auto st = make_cb(256ull * kShadowVpSlots * kFrameCount, shadow_frame_cb_); !st) {
      return st;
    }
    if (auto st = make_cb(256ull * kMaxLitDraws * kFrameCount, object_cb_); !st) {
      return st;
    }
    return Status::Ok();
  }

  Status CreateSwapchain() {
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

  Status CreateOffscreenBackbuffers() {
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
      backbuffers_[i].Reset();
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

  void TryEnableDisplayHdr() {
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
      if (!gpu_headless_) {
        backbuffers_[i].Reset();
        HRESULT hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i]));
        if (FAILED(hr)) {
          return Status::Fail("SwapChain::GetBuffer failed");
        }
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

  Status CreatePostColorTargets() {
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

  void UpdatePostSrvs() {
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

  // Wait for previously *submitted* GPU work. Does not mark in-flight frame
  // allocators idle — stamping fence_values_ mid-frame caused DEVICE_REMOVED
  // when ScreenQuad/debug buffers grew (e.g. after wheel zoom / particles).
  void WaitGpuSubmitted() {
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

  // Full idle: safe to Reset all frame allocators afterward.
  void WaitGpu() {
    WaitGpuSubmitted();
    for (auto& v : fence_values_) {
      v = fence_value_;
    }
  }

  Status EnsureColorReadbackBuffer() {
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

  Status EnsureDepthReadbackBuffer() {
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

  Status CreateGpuTimestampResources() {
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

  void ReadbackGpuPassTimings(std::uint32_t frame) {
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

  HWND hwnd_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool gpu_headless_ = false;
  int adapter_index_ = -1;
  bool vsync_ = false;
  bool allow_tearing_ = false;
  bool enable_hdr_output_ = false;
  bool bindless_capable_ = false;
  SIZE_T bindless_probe_gpu_ptr_ = 0;
  bool hdr_output_active_ = false;
  UINT offscreen_bb_index_ = 0;
  SubmitConfig submit_cfg_{};
  std::array<ComPtr<ID3D12CommandAllocator>, 4> worker_allocators_{};
  bool mesh_ready_ = false;
  bool lit_ready_ = false;
  bool quad_ready_ = false;
  bool debug_ready_ = false;
  bool ui_ready_ = false;
  bool ui_font_uploaded_ = false;
  bool post_ready_ = false;
  bool post_resolved_this_frame_ = false;
  bool shadow_active_ = false;
  bool local_shadow_active_ = false;
  int bound_shadow_slot_ = 0;
  std::uint32_t compute_dispatches_ = 0;
  bool cull_ready_ = false;
  ComPtr<ID3D12RootSignature> cull_root_;
  ComPtr<ID3D12PipelineState> cull_pso_;
  ComPtr<ID3D12Resource> cull_compact_buf_;
  UINT64 cull_compact_bytes_ = 0;
  D3D12_RESOURCE_STATES cull_compact_state_ = D3D12_RESOURCE_STATE_COMMON;
  std::uint32_t lit_draws_ = 0;
  std::uint32_t shadow_draws_ = 0;
  std::uint32_t screen_quad_draws_ = 0;
  FrameLighting lighting_{};
  D3D12_RESOURCE_STATES shadow_map_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES local_shadow_map_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES depth_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES scene_color_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES history_state_ = D3D12_RESOURCE_STATE_COMMON;
  UINT quad_vb_capacity_ = 0;
  UINT ui_vb_capacity_ = 0;
  UINT ui_ib_capacity_ = 0;

  ComPtr<IDXGIFactory6> factory_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<IDXGISwapChain3> swapchain_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  ComPtr<ID3D12DescriptorHeap> srv_heap_;
  ComPtr<ID3D12DescriptorHeap> shadow_dsv_heap_;
  ComPtr<ID3D12DescriptorHeap> local_shadow_dsv_heap_;
  ComPtr<ID3D12DescriptorHeap> shadow_srv_heap_;
  ComPtr<ID3D12DescriptorHeap> post_srv_heap_;
  ComPtr<ID3D12DescriptorHeap> hdr_rtv_heap_;
  UINT rtv_descriptor_size_ = 0;
  UINT cbv_srv_uav_descriptor_size_ = 0;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> backbuffers_{};
  std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> allocators_{};
  ComPtr<ID3D12GraphicsCommandList> command_list_;
  ComPtr<ID3D12Resource> dsv_;
  ComPtr<ID3D12Resource> shadow_map_;
  ComPtr<ID3D12Resource> local_shadow_map_;
  ComPtr<ID3D12Resource> lit_albedo_;
  ComPtr<ID3D12Resource> lit_orm_;
  ComPtr<ID3D12Resource> lit_albedo2_;
  ComPtr<ID3D12Resource> lit_orm2_;
  ComPtr<ID3D12Resource> reflection_cube_;   // t10 Fresnel / local probe
  ComPtr<ID3D12Resource> ibl_prefilter_;     // t6 IBL specular prefilter
  ComPtr<ID3D12Resource> ibl_irradiance_;    // t7
  ComPtr<ID3D12RootSignature> sky_root_;
  ComPtr<ID3D12PipelineState> sky_pso_;
  ComPtr<ID3D12DescriptorHeap> sky_srv_heap_;
  ComPtr<ID3D12Resource> sky_cb_;
  ComPtr<ID3D12Resource> sky_cube_;
  bool sky_ready_ = false;
  bool sky_uploaded_ = false;
  ComPtr<ID3D12Resource> ibl_brdf_lut_;
  std::array<MeshSlotGpu, kMaxMeshSlots> mesh_slots_{};
  ComPtr<ID3D12Resource> scene_color_;
  ComPtr<ID3D12Resource> history_;
  ComPtr<ID3D12Resource> color_readback_;
  std::uint32_t color_readback_w_ = 0;
  std::uint32_t color_readback_h_ = 0;
  ComPtr<ID3D12Resource> depth_readback_;
  std::uint32_t depth_readback_w_ = 0;
  std::uint32_t depth_readback_h_ = 0;
  ColorRgba last_clear_{0.14f, 0.16f, 0.20f, 1.f};
  ComPtr<ID3D12Resource> vertex_buffer_;
  ComPtr<ID3D12Resource> texture_;
  ComPtr<ID3D12Resource> texture_upload_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pso_;
  D3D12_VERTEX_BUFFER_VIEW vbv_{};
  ComPtr<ID3D12RootSignature> lit_root_;
  ComPtr<ID3D12PipelineState> lit_pso_;
  ComPtr<ID3D12PipelineState> lit_pso_transparent_;
  ComPtr<ID3D12RootSignature> shadow_root_;
  ComPtr<ID3D12PipelineState> shadow_pso_;
  ComPtr<ID3D12RootSignature> quad_root_;
  ComPtr<ID3D12PipelineState> quad_pso_;
  ComPtr<ID3D12RootSignature> debug_root_;
  ComPtr<ID3D12PipelineState> debug_pso_;
  ComPtr<ID3D12Resource> debug_cb_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> debug_vb_{};
  UINT debug_vb_capacity_ = 0;
  ComPtr<ID3D12RootSignature> post_root_;
  ComPtr<ID3D12PipelineState> post_pso_;
  ComPtr<ID3D12Resource> frame_cb_;
  ComPtr<ID3D12Resource> shadow_frame_cb_;
  ComPtr<ID3D12Resource> object_cb_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> instance_bufs_{};
  std::array<UINT64, kFrameCount> instance_buf_bytes_{};
  ComPtr<ID3D12Resource> indirect_args_buf_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> indirect_args_upload_{};
  ComPtr<ID3D12Resource> indirect_zero_upload_;
  UINT64 indirect_args_bytes_ = 0;
  std::array<UINT64, kFrameCount> indirect_args_upload_bytes_{};
  D3D12_RESOURCE_STATES indirect_args_state_ = D3D12_RESOURCE_STATE_COMMON;
  ComPtr<ID3D12CommandSignature> draw_indexed_cmd_sig_;
  ComPtr<ID3D12Resource> probe_face_color_;
  ComPtr<ID3D12Resource> probe_face_depth_;
  ComPtr<ID3D12DescriptorHeap> probe_rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> probe_dsv_heap_;
  int probe_face_size_ = 0;
  int probe_cube_size_ = 0;
  D3D12_RESOURCE_STATES reflection_cube_state_ = D3D12_RESOURCE_STATE_COMMON;
  ComPtr<ID3D12Resource> post_cb_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> quad_vb_{};
  ComPtr<ID3D12RootSignature> ui_root_;
  ComPtr<ID3D12PipelineState> ui_pso_;
  ComPtr<ID3D12Resource> ui_cb_;
  ComPtr<ID3D12Resource> ui_font_;
  ComPtr<ID3D12Resource> ui_font_upload_;
  ComPtr<ID3D12DescriptorHeap> ui_srv_heap_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> ui_vb_{};
  std::array<ComPtr<ID3D12Resource>, kFrameCount> ui_ib_{};
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  UINT64 fence_value_ = 0;
  std::array<UINT64, kFrameCount> fence_values_{};
  std::uint32_t frame_index_ = 0;

  ComPtr<ID3D12QueryHeap> timestamp_heap_;
  ComPtr<ID3D12Resource> timestamp_readback_;
  UINT64 timestamp_freq_ = 0;
  std::string gpu_pass_names_[kMaxGpuPasses];
  UINT gpu_pass_count_ = 0;
  UINT timestamp_cursor_ = 0;
  std::array<UINT, kFrameCount> frame_gpu_pass_counts_{};
  std::array<std::array<std::string, kMaxGpuPasses>, kFrameCount> frame_gpu_pass_names_{};
  std::array<bool, kFrameCount> frame_timestamps_pending_{};
  std::vector<GpuPassTiming> last_gpu_timings_;
};

}  // namespace

Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc& desc) {
  auto device = std::make_unique<D3D12Device>();
  if (auto st = device->Init(desc); !st) {
    return Result<std::unique_ptr<IDevice>>::Fail(st);
  }
  return Result<std::unique_ptr<IDevice>>::Ok(std::unique_ptr<IDevice>(std::move(device)));
}

std::vector<GpuAdapterInfo> EnumerateD3D12Adapters() {
  std::vector<GpuAdapterInfo> out;
  ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
    return out;
  }
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 ad{};
    adapter->GetDesc1(&ad);
    GpuAdapterInfo info;
    info.index = static_cast<int>(i);
    info.software = (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    info.dedicated_memory_bytes = ad.DedicatedVideoMemory;
    info.discrete = !info.software && ad.DedicatedVideoMemory > (512ull << 20);
    char name_utf8[256]{};
    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name_utf8, sizeof(name_utf8), nullptr,
                        nullptr);
    info.name = name_utf8;
    out.push_back(std::move(info));
  }
  return out;
}

}  // namespace engine::rhi
