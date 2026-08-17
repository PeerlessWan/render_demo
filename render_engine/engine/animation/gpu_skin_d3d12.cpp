#include "engine/animation/gpu_skin_d3d12.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace engine::animation {
namespace {

std::filesystem::path ResolveSkinCsPath(const std::filesystem::path& cs_dxil) {
  if (!cs_dxil.empty()) {
    return cs_dxil;
  }
#if defined(ENGINE_SHADER_DIR_A)
  const auto from_def = std::filesystem::path(ENGINE_SHADER_DIR_A) / "skin_cs.cso";
  if (std::filesystem::exists(from_def)) {
    return from_def;
  }
#endif
  if (const char* env = std::getenv("ENGINE_SHADER_DIR")) {
    const auto from_env = std::filesystem::path(env) / "skin_cs.cso";
    if (std::filesystem::exists(from_env)) {
      return from_env;
    }
  }
  return {};
}

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;

bool LoadFileBytes(const std::filesystem::path& path, std::vector<char>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !out.empty();
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heap,
                                    D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = heap;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  ComPtr<ID3D12Resource> res;
  if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                             IID_PPV_ARGS(&res)))) {
    return nullptr;
  }
  return res;
}

void Upload(ID3D12Resource* upload, const void* data, size_t bytes) {
  void* mapped = nullptr;
  if (SUCCEEDED(upload->Map(0, nullptr, &mapped)) && mapped) {
    std::memcpy(mapped, data, bytes);
    upload->Unmap(0, nullptr);
  }
}

Status DispatchGpuSkinD3d12Impl(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                                const std::vector<int>& bones4, const std::vector<float>& weights4,
                                std::vector<Vec3>& out_positions,
                                const std::filesystem::path& cs_dxil) {
  if (!QueryFeature("gpu_skinning")) {
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinD3d12: Feature gpu_skinning off");
  }
  if (bind_positions.empty()) {
    out_positions.clear();
    return Status::Ok();
  }
  const auto cs_path = ResolveSkinCsPath(cs_dxil);
  if (cs_path.empty()) {
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinD3d12: skin_cs.cso not found");
  }
  std::vector<char> cs_bytes;
  if (!LoadFileBytes(cs_path, cs_bytes)) {
    return Status::Fail(ErrorCode::Unavailable,
                        "DispatchGpuSkinD3d12: failed to read " + cs_path.string());
  }

  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinD3d12: DXGI factory failed");
  }

  ComPtr<ID3D12Device> device;
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      continue;
    }
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
        device) {
      break;
    }
    device.Reset();
  }
  if (!device) {
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinD3d12: no D3D12 device");
  }

  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (FAILED(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue))) ||
      FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr,
                                      IID_PPV_ARGS(&list)))) {
    return Status::Fail(ErrorCode::Failed, "DispatchGpuSkinD3d12: command objects failed");
  }

  // Root: b0 constants + t0..t3 SRV + u0 UAV (root descriptors).
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 4;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].RegisterSpace = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].RegisterSpace = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 4;

  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.Num32BitValues = 4;
  params[0].Constants.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 2;
  params[1].DescriptorTable.pDescriptorRanges = ranges;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rs{};
  rs.NumParameters = 2;
  rs.pParameters = params;
  rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
    return Status::Fail(ErrorCode::Failed, "DispatchGpuSkinD3d12: serialize root failed");
  }
  ComPtr<ID3D12RootSignature> root;
  if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&root)))) {
    return Status::Fail(ErrorCode::Failed, "DispatchGpuSkinD3d12: create root failed");
  }
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root.Get();
  pso_desc.CS = {cs_bytes.data(), cs_bytes.size()};
  ComPtr<ID3D12PipelineState> pso;
  if (FAILED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)))) {
    return Status::Fail(ErrorCode::Failed, "DispatchGpuSkinD3d12: create CS PSO failed");
  }

  const UINT n = static_cast<UINT>(bind_positions.size());
  const UINT bone_count =
      pose.bone_matrices.empty() ? 1u : static_cast<UINT>(pose.bone_matrices.size());

  struct Align16 {
    float m[16];
  };
  std::vector<Align16> bone_upload(bone_count);
  for (UINT i = 0; i < bone_count; ++i) {
    if (i < pose.bone_matrices.size()) {
      std::memcpy(bone_upload[i].m, pose.bone_matrices[i].m.data(), sizeof(float) * 16);
    } else {
      const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      std::memcpy(bone_upload[i].m, id, sizeof(id));
    }
  }
  struct Int4 {
    int v[4];
  };
  struct Float4 {
    float v[4];
  };
  std::vector<Int4> idx_upload(n);
  std::vector<Float4> w_upload(n);
  for (UINT i = 0; i < n; ++i) {
    idx_upload[i] = {0, 0, 0, 0};
    w_upload[i] = {1.f, 0.f, 0.f, 0.f};
    if (bones4.size() >= (i + 1ull) * 4 && weights4.size() >= (i + 1ull) * 4) {
      for (int k = 0; k < 4; ++k) {
        idx_upload[i].v[k] = bones4[i * 4ull + static_cast<std::size_t>(k)];
        w_upload[i].v[k] = weights4[i * 4ull + static_cast<std::size_t>(k)];
      }
    }
  }

  // float3 structured stride is 12 bytes.
  const UINT64 pos_bytes = static_cast<UINT64>(n) * 12ull;
  const UINT64 bone_bytes = static_cast<UINT64>(bone_count) * 64ull;
  const UINT64 idx_bytes = static_cast<UINT64>(n) * 16ull;
  const UINT64 w_bytes = static_cast<UINT64>(n) * 16ull;

  auto bind_up = CreateBuffer(device.Get(), pos_bytes, D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  auto bone_up = CreateBuffer(device.Get(), bone_bytes, D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  auto idx_up = CreateBuffer(device.Get(), idx_bytes, D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  auto w_up = CreateBuffer(device.Get(), w_bytes, D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  auto out_default =
      CreateBuffer(device.Get(), pos_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto out_readback = CreateBuffer(device.Get(), pos_bytes, D3D12_HEAP_TYPE_READBACK,
                                   D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
  if (!bind_up || !bone_up || !idx_up || !w_up || !out_default || !out_readback) {
    return Status::Fail(ErrorCode::Failed, "DispatchGpuSkinD3d12: buffer create failed");
  }

  {
    void* mapped = nullptr;
    if (SUCCEEDED(bind_up->Map(0, nullptr, &mapped)) && mapped) {
      auto* dst = static_cast<float*>(mapped);
      for (UINT i = 0; i < n; ++i) {
        dst[i * 3 + 0] = bind_positions[i].x;
        dst[i * 3 + 1] = bind_positions[i].y;
        dst[i * 3 + 2] = bind_positions[i].z;
      }
      bind_up->Unmap(0, nullptr);
    }
  }
  Upload(bone_up.Get(), bone_upload.data(), static_cast<size_t>(bone_bytes));
  Upload(idx_up.Get(), idx_upload.data(), static_cast<size_t>(idx_bytes));
  Upload(w_up.Get(), w_upload.data(), static_cast<size_t>(w_bytes));

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 5;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> heap;
  if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)))) {
    return Status::Fail(ErrorCode::Failed, "DispatchGpuSkinD3d12: descriptor heap failed");
  }
  const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();

  auto write_srv_float3 = [&](ID3D12Resource* res, UINT count) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = count;
    srv.Buffer.StructureByteStride = 12;
    device->CreateShaderResourceView(res, &srv, cpu);
    cpu.ptr += incr;
  };
  auto write_srv_mat = [&](ID3D12Resource* res, UINT count) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = count;
    srv.Buffer.StructureByteStride = 64;
    device->CreateShaderResourceView(res, &srv, cpu);
    cpu.ptr += incr;
  };
  auto write_srv_16 = [&](ID3D12Resource* res, UINT count) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = count;
    srv.Buffer.StructureByteStride = 16;
    device->CreateShaderResourceView(res, &srv, cpu);
    cpu.ptr += incr;
  };

  // Upload heaps can be SRV'd directly for this probe (GENERIC_READ).
  write_srv_float3(bind_up.Get(), n);
  write_srv_mat(bone_up.Get(), bone_count);
  write_srv_16(idx_up.Get(), n);
  write_srv_16(w_up.Get(), n);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.Buffer.FirstElement = 0;
  uav.Buffer.NumElements = n;
  uav.Buffer.StructureByteStride = 12;
  device->CreateUnorderedAccessView(out_default.Get(), nullptr, &uav, cpu);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = out_default.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);

  ID3D12DescriptorHeap* heaps[] = {heap.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootSignature(root.Get());
  list->SetPipelineState(pso.Get());
  const UINT constants[4] = {n, 0, 0, 0};
  list->SetComputeRoot32BitConstants(0, 4, constants, 0);
  list->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
  list->Dispatch((n + 63u) / 64u, 1, 1);

  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);
  list->CopyResource(out_readback.Get(), out_default.Get());
  list->Close();

  ID3D12CommandList* lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);

  ComPtr<ID3D12Fence> fence;
  UINT64 fence_val = 1;
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
    return Status::Fail(ErrorCode::Failed, "DispatchGpuSkinD3d12: fence failed");
  }
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  queue->Signal(fence.Get(), fence_val);
  if (fence->GetCompletedValue() < fence_val) {
    fence->SetEventOnCompletion(fence_val, event);
    WaitForSingleObject(event, INFINITE);
  }
  if (event) {
    CloseHandle(event);
  }

  out_positions.resize(n);
  {
    void* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(pos_bytes)};
    if (SUCCEEDED(out_readback->Map(0, &range, &mapped)) && mapped) {
      const auto* src = static_cast<const float*>(mapped);
      for (UINT i = 0; i < n; ++i) {
        out_positions[i] = Vec3{src[i * 3 + 0], src[i * 3 + 1], src[i * 3 + 2]};
      }
      D3D12_RANGE empty{0, 0};
      out_readback->Unmap(0, &empty);
    }
  }

  LogInfo("TryDispatchGpuSkinD3d12: CS skinned " + std::to_string(n) + " verts via " +
          cs_path.string());
  return Status::Ok();
}
#endif  // _WIN32

}  // namespace

Status DispatchGpuSkinD3d12Status(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                                  const std::vector<int>& bones4,
                                  const std::vector<float>& weights4,
                                  std::vector<Vec3>& out_positions,
                                  const std::filesystem::path& cs_dxil) {
#if defined(_WIN32)
  return DispatchGpuSkinD3d12Impl(bind_positions, pose, bones4, weights4, out_positions, cs_dxil);
#else
  (void)bind_positions;
  (void)pose;
  (void)bones4;
  (void)weights4;
  (void)out_positions;
  (void)cs_dxil;
  return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinD3d12: requires Windows/D3D12");
#endif
}

bool TryDispatchGpuSkinD3d12(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                             const std::vector<int>& bones4, const std::vector<float>& weights4,
                             std::vector<Vec3>& out_positions,
                             const std::filesystem::path& cs_dxil) {
  return DispatchGpuSkinD3d12Status(bind_positions, pose, bones4, weights4, out_positions, cs_dxil)
      .ok();
}

}  // namespace engine::animation
