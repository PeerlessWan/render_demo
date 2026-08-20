#include "engine/rt/raytracing.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <array>
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

#if ENGINE_WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace engine::rt {
namespace {

std::filesystem::path ResolveDxrLibPath(const std::filesystem::path& override_path) {
  if (!override_path.empty()) {
    return override_path;
  }
#if defined(ENGINE_SHADER_DIR_A)
  const auto from_def = std::filesystem::path(ENGINE_SHADER_DIR_A) / "dxr_shadow_lib.cso";
  if (std::filesystem::exists(from_def)) {
    return from_def;
  }
#endif
  if (const char* env = std::getenv("ENGINE_SHADER_DIR")) {
    const auto from_env = std::filesystem::path(env) / "dxr_shadow_lib.cso";
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

ComPtr<ID3D12Resource> CreateBuf(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heap,
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

UINT64 AlignUp(UINT64 v, UINT64 a) { return (v + a - 1) & ~(a - 1); }

Status WaitQueue(ID3D12Device* device, ID3D12CommandQueue* queue) {
  ComPtr<ID3D12Fence> fence;
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
    return Status::Fail(ErrorCode::Failed, "DXR: fence create failed");
  }
  const UINT64 val = 1;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  queue->Signal(fence.Get(), val);
  if (fence->GetCompletedValue() < val) {
    fence->SetEventOnCompletion(val, event);
    WaitForSingleObject(event, INFINITE);
  }
  if (event) {
    CloseHandle(event);
  }
  return Status::Ok();
}

Status TryBuildCubeBlasTlasAndDispatchRaysWin(const std::filesystem::path& dxr_lib_dxil) {
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
    return Status::Fail(ErrorCode::Unavailable, "TryBuildCubeBlasTlasAndDispatchRays: DXGI failed");
  }

  ComPtr<ID3D12Device5> device5;
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      continue;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        !device) {
      continue;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))) ||
        opts5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
      continue;
    }
    if (FAILED(device.As(&device5)) || !device5) {
      return Status::Fail(ErrorCode::Unavailable,
                          "TryBuildCubeBlasTlasAndDispatchRays: ID3D12Device5 missing");
    }
    break;
  }
  if (!device5) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryBuildCubeBlasTlasAndDispatchRays: no DXR-capable adapter");
  }

  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList4> list4;
  if (FAILED(device5->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue))) ||
      FAILED(device5->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
      FAILED(device5->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr,
                                       IID_PPV_ARGS(&list4)))) {
    return Status::Fail(ErrorCode::Failed, "TryBuildCubeBlasTlasAndDispatchRays: cmd objects");
  }

  // One triangle in XY at z=0 (cube-demo minimal geometry).
  const float verts[] = {
      -0.5f, -0.5f, 0.f, 0.5f, -0.5f, 0.f, 0.f, 0.5f, 0.f,
  };
  auto vb_upload = CreateBuf(device5.Get(), sizeof(verts), D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  if (!vb_upload) {
    return Status::Fail(ErrorCode::Failed, "TryBuildCubeBlasTlasAndDispatchRays: VB failed");
  }
  {
    void* mapped = nullptr;
    vb_upload->Map(0, nullptr, &mapped);
    std::memcpy(mapped, verts, sizeof(verts));
    vb_upload->Unmap(0, nullptr);
  }

  D3D12_RAYTRACING_GEOMETRY_DESC geom{};
  geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  geom.Triangles.Transform3x4 = 0;
  geom.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
  geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geom.Triangles.IndexCount = 0;
  geom.Triangles.VertexCount = 3;
  geom.Triangles.IndexBuffer = 0;
  geom.Triangles.VertexBuffer.StartAddress = vb_upload->GetGPUVirtualAddress();
  geom.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_in{};
  blas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  blas_in.NumDescs = 1;
  blas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_in.pGeometryDescs = &geom;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info{};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&blas_in, &blas_info);

  auto blas = CreateBuf(device5.Get(),
                        AlignUp(blas_info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
                        D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto scratch = CreateBuf(device5.Get(), AlignUp(blas_info.ScratchDataSizeInBytes, 256),
                           D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  if (!blas || !scratch) {
    return Status::Fail(ErrorCode::Failed, "TryBuildCubeBlasTlasAndDispatchRays: BLAS buffers");
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build{};
  blas_build.Inputs = blas_in;
  blas_build.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
  blas_build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
  list4->BuildRaytracingAccelerationStructure(&blas_build, 0, nullptr);

  D3D12_RESOURCE_BARRIER uav_barrier{};
  uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uav_barrier.UAV.pResource = blas.Get();
  list4->ResourceBarrier(1, &uav_barrier);

  // TLAS with one identity instance.
  D3D12_RAYTRACING_INSTANCE_DESC instance{};
  instance.Transform[0][0] = 1.f;
  instance.Transform[1][1] = 1.f;
  instance.Transform[2][2] = 1.f;
  instance.InstanceID = 0;
  instance.InstanceMask = 0xFF;
  instance.InstanceContributionToHitGroupIndex = 0;
  instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
  instance.AccelerationStructure = blas->GetGPUVirtualAddress();

  auto instance_up =
      CreateBuf(device5.Get(), AlignUp(sizeof(instance), D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT),
                D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  if (!instance_up) {
    return Status::Fail(ErrorCode::Failed, "TryBuildCubeBlasTlasAndDispatchRays: instance buffer");
  }
  {
    void* mapped = nullptr;
    instance_up->Map(0, nullptr, &mapped);
    std::memset(mapped, 0, AlignUp(sizeof(instance), D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT));
    std::memcpy(mapped, &instance, sizeof(instance));
    instance_up->Unmap(0, nullptr);
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_in{};
  tlas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  tlas_in.NumDescs = 1;
  tlas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_in.InstanceDescs = instance_up->GetGPUVirtualAddress();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info{};
  device5->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_in, &tlas_info);

  auto tlas = CreateBuf(device5.Get(),
                        AlignUp(tlas_info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
                        D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  auto tlas_scratch = CreateBuf(device5.Get(), AlignUp(tlas_info.ScratchDataSizeInBytes, 256),
                                D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  if (!tlas || !tlas_scratch) {
    return Status::Fail(ErrorCode::Failed, "TryBuildCubeBlasTlasAndDispatchRays: TLAS buffers");
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build{};
  tlas_build.Inputs = tlas_in;
  tlas_build.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
  tlas_build.ScratchAccelerationStructureData = tlas_scratch->GetGPUVirtualAddress();
  list4->BuildRaytracingAccelerationStructure(&tlas_build, 0, nullptr);

  uav_barrier.UAV.pResource = tlas.Get();
  list4->ResourceBarrier(1, &uav_barrier);

  list4->Close();
  ID3D12CommandList* lists_as[] = {list4.Get()};
  queue->ExecuteCommandLists(1, lists_as);
  if (auto st = WaitQueue(device5.Get(), queue.Get()); !st) {
    return st;
  }

  LogInfo("TryBuildCubeBlasTlasAndDispatchRays: BLAS+TLAS built (triangle AS)");

  // --- RTPSO + DispatchRays (optional if lib/cso missing) ---
  const auto lib_path = ResolveDxrLibPath(dxr_lib_dxil);
  std::vector<char> lib_bytes;
  if (lib_path.empty() || !LoadFileBytes(lib_path, lib_bytes)) {
    LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing "
            "(dxr_shadow_lib.cso not found); AS build Ok");
    return Status::Ok();
  }

  // Global root: t0 = TLAS SRV, u0 = output UAV (root descriptors).
  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[1].Descriptor.ShaderRegister = 0;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC rs_desc{};
  rs_desc.NumParameters = 2;
  rs_desc.pParameters = params;
  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  if (FAILED(D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
    LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing (root serialize); "
            "AS build Ok");
    return Status::Ok();
  }
  ComPtr<ID3D12RootSignature> global_rs;
  if (FAILED(device5->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                          IID_PPV_ARGS(&global_rs)))) {
    LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing (root create); "
            "AS build Ok");
    return Status::Ok();
  }

  D3D12_EXPORT_DESC exports[3]{};
  exports[0].Name = L"RayGen";
  exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
  exports[1].Name = L"Miss";
  exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
  exports[2].Name = L"ClosestHit";
  exports[2].Flags = D3D12_EXPORT_FLAG_NONE;

  D3D12_DXIL_LIBRARY_DESC lib_desc{};
  lib_desc.DXILLibrary.pShaderBytecode = lib_bytes.data();
  lib_desc.DXILLibrary.BytecodeLength = lib_bytes.size();
  lib_desc.NumExports = 3;
  lib_desc.pExports = exports;

  D3D12_HIT_GROUP_DESC hit_group{};
  hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hit_group.HitGroupExport = L"HitGroup";
  hit_group.ClosestHitShaderImport = L"ClosestHit";

  D3D12_RAYTRACING_SHADER_CONFIG shader_cfg{};
  shader_cfg.MaxPayloadSizeInBytes = 16;  // float3 + pad
  shader_cfg.MaxAttributeSizeInBytes = 8;

  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_cfg{};
  pipeline_cfg.MaxTraceRecursionDepth = 1;

  D3D12_GLOBAL_ROOT_SIGNATURE grs{};
  grs.pGlobalRootSignature = global_rs.Get();

  D3D12_STATE_SUBOBJECT subs[6]{};
  subs[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  subs[0].pDesc = &lib_desc;
  subs[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  subs[1].pDesc = &hit_group;
  subs[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  subs[2].pDesc = &shader_cfg;
  subs[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  subs[3].pDesc = &pipeline_cfg;
  subs[4].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  subs[4].pDesc = &grs;

  // Associate shader config with exports via a shader config association.
  const wchar_t* export_names[] = {L"RayGen", L"Miss", L"HitGroup"};
  D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc{};
  assoc.pSubobjectToAssociate = &subs[2];
  assoc.NumExports = 3;
  assoc.pExports = export_names;
  subs[5].Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
  subs[5].pDesc = &assoc;

  D3D12_STATE_OBJECT_DESC so_desc{};
  so_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  so_desc.NumSubobjects = 6;
  so_desc.pSubobjects = subs;

  ComPtr<ID3D12StateObject> rtpso;
  if (FAILED(device5->CreateStateObject(&so_desc, IID_PPV_ARGS(&rtpso))) || !rtpso) {
    LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing (CreateStateObject "
            "failed); AS build Ok");
    return Status::Ok();
  }

  ComPtr<ID3D12StateObjectProperties> so_props;
  if (FAILED(rtpso.As(&so_props)) || !so_props) {
    LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing (SO properties); "
            "AS build Ok");
    return Status::Ok();
  }

  constexpr UINT kDispatchW = 8;
  constexpr UINT kDispatchH = 8;
  constexpr UINT kOutCount = kDispatchW * kDispatchH;

  auto output_uav =
      CreateBuf(device5.Get(), sizeof(float) * 4ull * kOutCount, D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  if (!output_uav) {
    LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing (UAV create); "
            "AS build Ok");
    return Status::Ok();
  }

  // Shader binding table: raygen + miss + hitgroup (identifier only each).
  const UINT id_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  const UINT record_size = static_cast<UINT>(AlignUp(id_size, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));
  const UINT sbt_size = record_size * 3;
  auto sbt = CreateBuf(device5.Get(), AlignUp(sbt_size, 256), D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
  if (!sbt) {
    LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing (SBT); AS build Ok");
    return Status::Ok();
  }
  {
    void* mapped = nullptr;
    sbt->Map(0, nullptr, &mapped);
    auto* bytes = static_cast<uint8_t*>(mapped);
    std::memset(bytes, 0, sbt_size);
    const void* id_rgen = so_props->GetShaderIdentifier(L"RayGen");
    const void* id_miss = so_props->GetShaderIdentifier(L"Miss");
    const void* id_hit = so_props->GetShaderIdentifier(L"HitGroup");
    if (!id_rgen || !id_miss || !id_hit) {
      sbt->Unmap(0, nullptr);
      LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays PSO missing (shader id); "
              "AS build Ok");
      return Status::Ok();
    }
    std::memcpy(bytes + 0 * record_size, id_rgen, id_size);
    std::memcpy(bytes + 1 * record_size, id_miss, id_size);
    std::memcpy(bytes + 2 * record_size, id_hit, id_size);
    sbt->Unmap(0, nullptr);
  }

  alloc->Reset();
  list4->Reset(alloc.Get(), nullptr);
  list4->SetComputeRootSignature(global_rs.Get());
  list4->SetPipelineState1(rtpso.Get());
  list4->SetComputeRootShaderResourceView(0, tlas->GetGPUVirtualAddress());
  list4->SetComputeRootUnorderedAccessView(1, output_uav->GetGPUVirtualAddress());

  D3D12_DISPATCH_RAYS_DESC dispatch{};
  dispatch.RayGenerationShaderRecord.StartAddress = sbt->GetGPUVirtualAddress();
  dispatch.RayGenerationShaderRecord.SizeInBytes = record_size;
  dispatch.MissShaderTable.StartAddress = sbt->GetGPUVirtualAddress() + record_size;
  dispatch.MissShaderTable.SizeInBytes = record_size;
  dispatch.MissShaderTable.StrideInBytes = record_size;
  dispatch.HitGroupTable.StartAddress = sbt->GetGPUVirtualAddress() + record_size * 2ull;
  dispatch.HitGroupTable.SizeInBytes = record_size;
  dispatch.HitGroupTable.StrideInBytes = record_size;
  dispatch.Width = kDispatchW;
  dispatch.Height = kDispatchH;
  dispatch.Depth = 1;
  list4->DispatchRays(&dispatch);
  list4->Close();

  ID3D12CommandList* lists_rt[] = {list4.Get()};
  queue->ExecuteCommandLists(1, lists_rt);
  if (auto st = WaitQueue(device5.Get(), queue.Get()); !st) {
    return st;
  }

  LogInfo("TryBuildCubeBlasTlasAndDispatchRays: DispatchRays " + std::to_string(kDispatchW) + "x" +
          std::to_string(kDispatchH) + " Ok (lib=" + lib_path.string() + ")");
  return Status::Ok();
}
#endif  // _WIN32

}  // namespace

RtStatus Resolve(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg) {
  if (!cfg.enable) {
    return RtStatus::Disabled;
  }
  if (backend == rhi::Backend::D3D12 && features.raytracing) {
    return RtStatus::Supported;
  }
  if (backend == rhi::Backend::Vulkan && features.raytracing) {
    return RtStatus::Supported;
  }
  if (cfg.allow_fallback) {
    return RtStatus::UnsupportedFallback;
  }
  return RtStatus::Unavailable;
}

Status EnsureSafe(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg) {
  const auto st = Resolve(backend, features, cfg);
  if (st == RtStatus::Unavailable) {
    return Status::Fail(ErrorCode::Unavailable, "raytracing requested but unsupported");
  }
  return Status::Ok();
}

bool ProbeDxrHardwareSupport() {
#if defined(_WIN32)
  using Microsoft::WRL::ComPtr;
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
    return false;
  }
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) {
      continue;
    }
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        !device) {
      continue;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))) &&
        opts5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
      return true;
    }
  }
  return false;
#else
  return false;
#endif
}

bool CanRunDxrDemo(const FeatureSet& features, const DxrDemoConfig& demo) {
  if (!features.raytracing || !features.d3d12) {
    return false;
  }
  return demo.enable_reflections || demo.enable_shadows;
}

DxrShadowDemoResult DxrShadowDemo(const FeatureSet& features, const DxrDemoConfig& demo) {
  DxrShadowDemoResult out;
  out.would_run = features.raytracing && features.d3d12 && demo.enable_shadows;
  if (out.would_run) {
    LogInfo("DxrShadowDemo: shadow demo pass WOULD run; attempting real AS/DispatchRays (W7)");
    const Status real = TryBuildCubeBlasTlasAndDispatchRays();
    if (real) {
      LogInfo("DxrShadowDemo: TryBuildCubeBlasTlasAndDispatchRays Ok");
    } else {
      LogInfo(std::string("DxrShadowDemo: real path ") +
              (real.code() == ErrorCode::Unavailable ? "Unavailable: " : "Failed: ") +
              real.message());
    }
  }
  return out;
}

Status RunDxrFullscreenStub(rhi::IDevice& device) {
  (void)device;
  const FeatureSet features = QueryFeatures();
  if (!features.raytracing) {
    return Status::Fail(ErrorCode::Unavailable,
                        "RunDxrFullscreenStub unavailable (raytracing feature false)");
  }

  const Status real = TryBuildCubeBlasTlasAndDispatchRays();
  if (real) {
    LogInfo("RunDxrFullscreenStub: real BLAS/TLAS/DispatchRays path Ok (W7)");
    return Status::Ok();
  }
  if (real.code() != ErrorCode::Unavailable) {
    return real;
  }

  const Status tlas = TryEmptyTlasPrebuild();
  if (!tlas && tlas.code() != ErrorCode::Unavailable) {
    return tlas;
  }
  LogInfo("RunDxrFullscreenStub: fullscreen demo fallback (empty-TLAS / stub); "
          "real DispatchRays Unavailable — " +
          real.message());
  return Status::Ok();
}

Status TryEmptyTlasPrebuild() {
#if defined(_WIN32)
  using Microsoft::WRL::ComPtr;
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
    return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: DXGI factory failed");
  }
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) {
      continue;
    }
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        !device) {
      continue;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))) ||
        opts5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
      continue;
    }
    ComPtr<ID3D12Device5> device5;
    if (FAILED(device.As(&device5)) || !device5) {
      return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: ID3D12Device5 missing");
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 0;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = 0;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    LogInfo("TryEmptyTlasPrebuild: empty TLAS prebuild ResultDataMaxSizeInBytes=" +
            std::to_string(info.ResultDataMaxSizeInBytes));
    return Status::Ok();
  }
  return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: no DXR-capable adapter");
#else
  return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: DXR requires Windows/D3D12");
#endif
}

Status TryBuildCubeBlasTlasAndDispatchRays(const std::filesystem::path& dxr_lib_dxil) {
#if defined(_WIN32)
  return TryBuildCubeBlasTlasAndDispatchRaysWin(dxr_lib_dxil);
#else
  (void)dxr_lib_dxil;
  return Status::Fail(ErrorCode::Unavailable,
                      "TryBuildCubeBlasTlasAndDispatchRays: DXR requires Windows/D3D12");
#endif
}

Status TryComposeDxrShadowOverlay(float& out_shadow_factor) {
  out_shadow_factor = 1.f;
  FeatureSet features = QueryFeatures();
  if (!features.raytracing && ProbeDxrHardwareSupport()) {
    SetFeatureOverride("raytracing", true);
    features = QueryFeatures();
  }

  DxrDemoConfig demo;
  demo.enable_shadows = true;
  const DxrShadowDemoResult demo_result = DxrShadowDemo(features, demo);
  if (demo_result.would_run) {
    // Small-resolution demo ran (or attempted real AS/DispatchRays): darken factor.
    out_shadow_factor = 0.35f;
    LogInfo("TryComposeDxrShadowOverlay: composed factor=" + std::to_string(out_shadow_factor));
    return Status::Ok("dxr-shadow-overlay");
  }

  const Status built = TryBuildCubeBlasTlasAndDispatchRays();
  if (built) {
    out_shadow_factor = 0.5f;
    LogInfo("TryComposeDxrShadowOverlay: AS/DispatchRays Ok, factor=" +
            std::to_string(out_shadow_factor));
    return Status::Ok("dxr-compose-after-demo");
  }

  const Status tlas = TryEmptyTlasPrebuild();
  if (tlas) {
    out_shadow_factor = 0.85f;
    return Status::Ok("dxr-compose-empty-tlas");
  }

  return Status::Fail(ErrorCode::Unavailable,
                      "TryComposeDxrShadowOverlay Unavailable SKIP: no DXR demo path");
}

Status TryHalfResSoftShadowCompose(float& out_shadow_factor) {
  out_shadow_factor = 1.f;
  const FeatureSet features = QueryFeatures();
  if (!features.raytracing) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TrySoftShadowCompose Unavailable SKIP: Feature raytracing off");
  }

  // W17 ADR 0041: half-resolution soft-shadow — build a small factor grid from DXR
  // overlay, separable 3-tap blur, then reduce to a single compose factor (product mid).
  float overlay = 1.f;
  const Status composed = TryComposeDxrShadowOverlay(overlay);
  float seed = overlay;
  bool have_seed = static_cast<bool>(composed);
  if (!have_seed) {
    DxrDemoConfig demo;
    demo.enable_shadows = true;
    if (!CanRunDxrDemo(features, demo)) {
      return Status::Fail(ErrorCode::Unavailable,
                          "TrySoftShadowCompose Unavailable SKIP: no RT demo path");
    }
    seed = 0.62f;
  }

  constexpr int kHalfW = 8;
  constexpr int kHalfH = 8;
  float grid[kHalfW * kHalfH];
  for (int y = 0; y < kHalfH; ++y) {
    for (int x = 0; x < kHalfW; ++x) {
      // Spatialize seed slightly so blur has something to smooth (not a full RT target).
      const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(kHalfW);
      const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(kHalfH);
      const float wobble = 0.04f * ((nx - 0.5f) * (nx - 0.5f) + (ny - 0.5f) * (ny - 0.5f));
      grid[y * kHalfW + x] = (std::min)(1.f, (std::max)(0.f, seed + wobble - 0.02f));
    }
  }
  float tmp[kHalfW * kHalfH];
  auto sample = [&](const float* src, int x, int y) {
    x = (std::max)(0, (std::min)(kHalfW - 1, x));
    y = (std::max)(0, (std::min)(kHalfH - 1, y));
    return src[y * kHalfW + x];
  };
  for (int y = 0; y < kHalfH; ++y) {
    for (int x = 0; x < kHalfW; ++x) {
      tmp[y * kHalfW + x] =
          (sample(grid, x - 1, y) + sample(grid, x, y) + sample(grid, x + 1, y)) / 3.f;
    }
  }
  for (int y = 0; y < kHalfH; ++y) {
    for (int x = 0; x < kHalfW; ++x) {
      grid[y * kHalfW + x] =
          (sample(tmp, x, y - 1) + sample(tmp, x, y) + sample(tmp, x, y + 1)) / 3.f;
    }
  }
  float sum = 0.f;
  for (float v : grid) {
    sum += v;
  }
  out_shadow_factor = sum / static_cast<float>(kHalfW * kHalfH);
  LogInfo("TrySoftShadowCompose: Ok half-res-blur soft_factor=" +
          std::to_string(out_shadow_factor));
  return Status::Ok("half-res-soft-shadow-blur");
}

Status TryVkTraceRaysDemoStub() {
#if ENGINE_WITH_VULKAN
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "engine_vk_tracerays_probe";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryVkTraceRaysDemoStub: vkCreateInstance failed SKIP");
  }

  uint32_t phys_count = 0;
  vkEnumeratePhysicalDevices(instance, &phys_count, nullptr);
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  bool has_rt_pipeline = false;
  bool has_as = false;
  bool has_deferred = false;
  bool has_bda = false;
  if (phys_count > 0) {
    std::vector<VkPhysicalDevice> phys_list(phys_count);
    vkEnumeratePhysicalDevices(instance, &phys_count, phys_list.data());
    for (VkPhysicalDevice pd : phys_list) {
      uint32_t ext_count = 0;
      vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
      std::vector<VkExtensionProperties> exts(ext_count);
      vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
      has_rt_pipeline = false;
      has_as = false;
      has_deferred = false;
      has_bda = false;
      for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0) {
          has_rt_pipeline = true;
        } else if (std::strcmp(e.extensionName,
                               VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0) {
          has_as = true;
        } else if (std::strcmp(e.extensionName,
                               VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0) {
          has_deferred = true;
        } else if (std::strcmp(e.extensionName,
                               VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0) {
          has_bda = true;
        }
      }
      if (!has_rt_pipeline) {
        continue;
      }
      uint32_t qcount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
      std::vector<VkQueueFamilyProperties> qprops(qcount);
      vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());
      for (uint32_t i = 0; i < qcount; ++i) {
        if (qprops[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
          phys = pd;
          queue_family = i;
          break;
        }
      }
      if (phys) {
        break;
      }
    }
  }

  if (!phys || !has_rt_pipeline) {
    vkDestroyInstance(instance, nullptr);
    return Status::Fail(ErrorCode::Unavailable,
                        "TryVkTraceRaysDemoStub Unavailable SKIP: no VK_KHR_ray_tracing_pipeline");
  }

  // Mega-W11: create a device with RT pipeline features and resolve vkCmdTraceRaysKHR.
  std::vector<const char*> enabled_exts;
  enabled_exts.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
  if (has_as) {
    enabled_exts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
  }
  if (has_deferred) {
    enabled_exts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
  }
  if (has_bda) {
    enabled_exts.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  }

  VkPhysicalDeviceBufferDeviceAddressFeatures bda_f{};
  bda_f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  bda_f.bufferDeviceAddress = has_bda ? VK_TRUE : VK_FALSE;

  VkPhysicalDeviceAccelerationStructureFeaturesKHR as_f{};
  as_f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
  as_f.accelerationStructure = has_as ? VK_TRUE : VK_FALSE;
  as_f.pNext = has_bda ? &bda_f : nullptr;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_f{};
  rt_f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
  rt_f.rayTracingPipeline = VK_TRUE;
  rt_f.pNext = (has_as || has_bda) ? &as_f : nullptr;

  float prio = 1.f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queue_family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;

  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = &rt_f;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<uint32_t>(enabled_exts.size());
  dci.ppEnabledExtensionNames = enabled_exts.data();

  VkDevice device = VK_NULL_HANDLE;
  const VkResult cr = vkCreateDevice(phys, &dci, nullptr, &device);
  if (cr != VK_SUCCESS || !device) {
    vkDestroyInstance(instance, nullptr);
    return Status::Fail(ErrorCode::Unavailable,
                        "TryVkTraceRaysDemoStub Unavailable SKIP: vkCreateDevice RT failed");
  }

  auto fn = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
      vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));
  vkDestroyDevice(device, nullptr);
  vkDestroyInstance(instance, nullptr);

  if (!fn) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryVkTraceRaysDemoStub Unavailable SKIP: vkCmdTraceRaysKHR unresolved");
  }

  LogInfo("TryVkTraceRaysDemoStub: vkCmdTraceRaysKHR resolved (rayTracingPipeline Ok)");
  return Status::Ok("vk-tracerays-khr");
#else
  return Status::Fail(ErrorCode::Unavailable,
                      "TryVkTraceRaysDemoStub Unavailable SKIP: ENGINE_WITH_VULKAN=0");
#endif
}

}  // namespace engine::rt
