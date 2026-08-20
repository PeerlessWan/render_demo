#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace engine::vfx {
namespace particles_detail {


struct ParticleGpu {
    float px, py, pz, life;
    float vx, vy, vz, size;
    float cr, cg, cb, ca;
};

std::filesystem::path ResolveParticleCsPath() {
#if defined(ENGINE_SHADER_DIR_A)
    const auto from_def = std::filesystem::path(ENGINE_SHADER_DIR_A) / "particle_integrate_cs.cso";
    if (std::filesystem::exists(from_def)) {
        return from_def;
    }
#endif
    if (const char* env = std::getenv("ENGINE_SHADER_DIR")) {
        const auto from_env = std::filesystem::path(env) / "particle_integrate_cs.cso";
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

Status TryIntegrateGpuCsD3d12(std::vector<Particle>& particles, float dt) {
    if (particles.empty()) {
        return Status::Ok("gpu-cs-d3d12-empty");
    }
    const auto cs_path = ResolveParticleCsPath();
    if (cs_path.empty()) {
        return Status::Fail(ErrorCode::Unavailable, "particle_integrate_cs.cso not found");
    }
    std::vector<char> cs_bytes;
    if (!LoadFileBytes(cs_path, cs_bytes)) {
        return Status::Fail(ErrorCode::Unavailable, "failed to read particle_integrate_cs.cso");
    }

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
        return Status::Fail(ErrorCode::Unavailable, "DXGI factory failed");
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
        return Status::Fail(ErrorCode::Unavailable, "no D3D12 device");
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
        return Status::Fail(ErrorCode::Failed, "command objects failed");
    }

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 4;
    params[0].Constants.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2;
    rs.pParameters = params;
    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
        return Status::Fail(ErrorCode::Failed, "serialize root failed");
    }
    ComPtr<ID3D12RootSignature> root;
    if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                                                 IID_PPV_ARGS(&root)))) {
        return Status::Fail(ErrorCode::Failed, "create root failed");
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = root.Get();
    pso_desc.CS = {cs_bytes.data(), cs_bytes.size()};
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)))) {
        return Status::Fail(ErrorCode::Failed, "create CS PSO failed");
    }

    const UINT n = static_cast<UINT>(particles.size());
    std::vector<ParticleGpu> upload(n);
    for (UINT i = 0; i < n; ++i) {
        upload[i].px = particles[i].position.x;
        upload[i].py = particles[i].position.y;
        upload[i].pz = particles[i].position.z;
        upload[i].life = particles[i].life;
        upload[i].vx = particles[i].velocity.x;
        upload[i].vy = particles[i].velocity.y;
        upload[i].vz = particles[i].velocity.z;
        upload[i].size = particles[i].size;
        upload[i].cr = particles[i].color.r;
        upload[i].cg = particles[i].color.g;
        upload[i].cb = particles[i].color.b;
        upload[i].ca = particles[i].color.a;
    }
    const UINT64 bytes = static_cast<UINT64>(n) * sizeof(ParticleGpu);
    auto uav_buf =
            CreateBuffer(device.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto up_buf = CreateBuffer(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    auto rb_buf = CreateBuffer(device.Get(), bytes, D3D12_HEAP_TYPE_READBACK,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    if (!uav_buf || !up_buf || !rb_buf) {
        return Status::Fail(ErrorCode::Failed, "buffer create failed");
    }
    {
        void* mapped = nullptr;
        if (SUCCEEDED(up_buf->Map(0, nullptr, &mapped)) && mapped) {
            std::memcpy(mapped, upload.data(), static_cast<size_t>(bytes));
            up_buf->Unmap(0, nullptr);
        }
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 1;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)))) {
        return Status::Fail(ErrorCode::Failed, "descriptor heap failed");
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.Buffer.NumElements = n;
    uav.Buffer.StructureByteStride = sizeof(ParticleGpu);
    device->CreateUnorderedAccessView(uav_buf.Get(), nullptr, &uav,
                                                                        heap->GetCPUDescriptorHandleForHeapStart());

    list->CopyBufferRegion(uav_buf.Get(), 0, up_buf.Get(), 0, bytes);
    D3D12_RESOURCE_BARRIER bar{};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = uav_buf.Get();
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &bar);

    list->SetPipelineState(pso.Get());
    list->SetComputeRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    float consts[4] = {dt, static_cast<float>(n), 0.f, 0.f};
    list->SetComputeRoot32BitConstants(0, 4, consts, 0);
    list->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch((n + 63u) / 64u, 1, 1);

    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &bar);
    list->CopyBufferRegion(rb_buf.Get(), 0, uav_buf.Get(), 0, bytes);
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    UINT64 fence_v = 1;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        return Status::Fail(ErrorCode::Failed, "fence failed");
    }
    HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    queue->Signal(fence.Get(), fence_v);
    if (fence->GetCompletedValue() < fence_v) {
        fence->SetEventOnCompletion(fence_v, evt);
        WaitForSingleObject(evt, INFINITE);
    }
    if (evt) {
        CloseHandle(evt);
    }

    {
        void* mapped = nullptr;
        D3D12_RANGE range_r{0, static_cast<SIZE_T>(bytes)};
        if (SUCCEEDED(rb_buf->Map(0, &range_r, &mapped)) && mapped) {
            auto* src = static_cast<const ParticleGpu*>(mapped);
            for (UINT i = 0; i < n; ++i) {
                particles[i].position = {src[i].px, src[i].py, src[i].pz};
                particles[i].life = src[i].life;
                particles[i].velocity = {src[i].vx, src[i].vy, src[i].vz};
                particles[i].size = src[i].size;
                particles[i].color = {src[i].cr, src[i].cg, src[i].cb, src[i].ca};
            }
            rb_buf->Unmap(0, nullptr);
        }
    }
    return Status::Ok("gpu-cs-d3d12");
}
#else
Status TryIntegrateGpuCsD3d12(std::vector<Particle>&, float) {
    return Status::Fail(ErrorCode::Unavailable, "D3D12 particle CS not available on this platform");
}
#endif

}  // namespace particles_detail
}  // namespace engine::vfx
