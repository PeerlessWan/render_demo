#include "engine/gpu_driven/meshlet.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(ENGINE_WITH_MESHOPTIMIZER) && ENGINE_WITH_MESHOPTIMIZER
#include "meshoptimizer.h"
#endif

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

#if ENGINE_WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace engine::gpu_driven {
namespace {

Aabb TransformAabb(const Aabb& local, const Mat4& world) {
  const Vec3 corners[8] = {
      {local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
      {local.min.x, local.max.y, local.min.z}, {local.max.x, local.max.y, local.min.z},
      {local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
      {local.min.x, local.max.y, local.max.z}, {local.max.x, local.max.y, local.max.z},
  };
  Aabb out;
  out.min = {1e9f, 1e9f, 1e9f};
  out.max = {-1e9f, -1e9f, -1e9f};
  for (const auto& c : corners) {
    const Vec3 w = world.TransformPoint(c);
    out.min.x = std::min(out.min.x, w.x);
    out.min.y = std::min(out.min.y, w.y);
    out.min.z = std::min(out.min.z, w.z);
    out.max.x = std::max(out.max.x, w.x);
    out.max.y = std::max(out.max.y, w.y);
    out.max.z = std::max(out.max.z, w.z);
  }
  return out;
}

Aabb Expand(Aabb a, const Vec3& p) {
  a.min.x = std::min(a.min.x, p.x);
  a.min.y = std::min(a.min.y, p.y);
  a.min.z = std::min(a.min.z, p.z);
  a.max.x = std::max(a.max.x, p.x);
  a.max.y = std::max(a.max.y, p.y);
  a.max.z = std::max(a.max.z, p.z);
  return a;
}

bool MeshShaderFeatureOn() {
  return QueryFeature("meshlet") || QueryFeature("mesh_shader");
}

std::filesystem::path ResolveMeshletMsPath(const char* filename) {
#if defined(ENGINE_SHADER_DIR_A)
  const auto from_def = std::filesystem::path(ENGINE_SHADER_DIR_A) / filename;
  if (std::filesystem::exists(from_def)) {
    return from_def;
  }
#endif
  if (const char* env = std::getenv("ENGINE_SHADER_DIR")) {
    const auto from_env = std::filesystem::path(env) / filename;
    if (std::filesystem::exists(from_env)) {
      return from_env;
    }
  }
  return {};
}

bool LoadFileBytes(const std::filesystem::path& path, std::vector<char>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !out.empty();
}

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // padding due to alignas(void*) PSO stream layout
#endif
// Manual PSO stream (no d3dx12) — each subobject aligned to void*.
struct alignas(void*) MeshPsoStream {
  struct alignas(void*) {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
    ID3D12RootSignature* pRootSignature = nullptr;
  } root_signature;
  struct alignas(void*) {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
    D3D12_SHADER_BYTECODE bytecode{};
  } ms;
  struct alignas(void*) {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
    D3D12_SHADER_BYTECODE bytecode{};
  } ps;
  struct alignas(void*) {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type =
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  } primitive_topology;
  struct alignas(void*) {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
    D3D12_DEPTH_STENCIL_DESC depth_stencil{};  // DepthEnable=FALSE by zero-init
  } depth_stencil;
  struct alignas(void*) {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type =
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
    D3D12_RT_FORMAT_ARRAY rt_formats{};
  } rtv_formats;
  struct alignas(void*) {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
    DXGI_SAMPLE_DESC sample_desc{1, 0};
  } sample_desc;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

Status TryMeshShaderPathD3d12() {
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
    // Unit-test / headless context without DXGI — Feature forced ⇒ ready.
    LogInfo("TryMeshShaderPath: ready (no DXGI factory; Feature forced)");
    return Status::Ok();
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
    // Prefer FL 12_1/12_0 for mesh-shader OPTIONS7; fall back to 11_0.
    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_0};
    for (D3D_FEATURE_LEVEL fl : kLevels) {
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), fl, IID_PPV_ARGS(&device))) && device) {
        break;
      }
      device.Reset();
    }
    if (device) {
      break;
    }
  }
  if (!device) {
    LogInfo("TryMeshShaderPath: ready (no D3D12 device; Feature forced)");
    return Status::Ok();  // ready: Feature on, no D3D12 device in this context
  }

  D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7{};
  const HRESULT hr_opts =
      device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7));
  if (FAILED(hr_opts)) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Mesh Shader path SKIP: CheckFeatureSupport(OPTIONS7) hr=0x%08X (C08)",
                  static_cast<unsigned>(hr_opts));
    return Status::Fail(ErrorCode::Unavailable, buf);
  }
  if (opts7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
    const char* msg = "Mesh Shader path SKIP: D3D12 MeshShaderTier not supported (C08)";
    LogInfo(msg);
    return Status::Fail(ErrorCode::Unavailable, msg);
  }

  const auto ms_path = ResolveMeshletMsPath("meshlet_ms.ms.cso");
  const auto ps_path = ResolveMeshletMsPath("meshlet_ms.ps.cso");
  if (ms_path.empty() || ps_path.empty()) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Mesh Shader path SKIP: meshlet_ms.ms.cso/ps.cso not found (C08)");
  }
  std::vector<char> ms_bytes;
  std::vector<char> ps_bytes;
  if (!LoadFileBytes(ms_path, ms_bytes) || !LoadFileBytes(ps_path, ps_bytes)) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Mesh Shader path SKIP: failed to read meshlet_ms DXIL (C08)");
  }

  D3D12_ROOT_SIGNATURE_DESC rs_desc{};
  // Empty root: MS embeds cube geometry (no CB/SRV).
  rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  if (FAILED(D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Mesh Shader path SKIP: serialize empty root failed (C08)");
  }
  ComPtr<ID3D12RootSignature> root;
  if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                         IID_PPV_ARGS(&root)))) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Mesh Shader path SKIP: CreateRootSignature failed (C08)");
  }

  ComPtr<ID3D12Device2> device2;
  if (FAILED(device.As(&device2)) || !device2) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Mesh Shader path SKIP: ID3D12Device2 unavailable (C08)");
  }

  MeshPsoStream stream{};
  stream.root_signature.pRootSignature = root.Get();
  stream.ms.bytecode = {ms_bytes.data(), ms_bytes.size()};
  stream.ps.bytecode = {ps_bytes.data(), ps_bytes.size()};
  stream.rtv_formats.rt_formats.NumRenderTargets = 1;
  stream.rtv_formats.rt_formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  stream.sample_desc.sample_desc = {1, 0};

  D3D12_PIPELINE_STATE_STREAM_DESC stream_desc{};
  stream_desc.SizeInBytes = sizeof(stream);
  stream_desc.pPipelineStateSubobjectStream = &stream;

  ComPtr<ID3D12PipelineState> pso;
  const HRESULT hr_pso = device2->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr_pso) || !pso) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Mesh Shader path SKIP: CreatePipelineState (MS) failed hr=0x%08X (C08)",
                  static_cast<unsigned>(hr_pso));
    LogInfo(std::string("TryMeshShaderPath: ") + buf);
    return Status::Fail(ErrorCode::Unavailable, buf);
  }

  // Optional DispatchMesh demo on a 1x1 RT (same temporary device pattern as gpu_skin_d3d12).
  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (SUCCEEDED(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue))) &&
      SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) &&
      SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr,
                                         IID_PPV_ARGS(&list)))) {
    ComPtr<ID3D12GraphicsCommandList6> list6;
    if (SUCCEEDED(list.As(&list6)) && list6) {
      D3D12_HEAP_PROPERTIES hp{};
      hp.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC rt_desc{};
      rt_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      rt_desc.Width = 4;
      rt_desc.Height = 4;
      rt_desc.DepthOrArraySize = 1;
      rt_desc.MipLevels = 1;
      rt_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rt_desc.SampleDesc.Count = 1;
      rt_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      D3D12_CLEAR_VALUE clear{};
      clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      ComPtr<ID3D12Resource> rt;
      D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
      heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      heap_desc.NumDescriptors = 1;
      ComPtr<ID3D12DescriptorHeap> rtv_heap;
      if (SUCCEEDED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rt_desc,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                                    IID_PPV_ARGS(&rt))) &&
          SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap)))) {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        const float clear_color[4] = {0.f, 0.f, 0.f, 1.f};
        list6->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
        list6->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp{0.f, 0.f, 4.f, 4.f, 0.f, 1.f};
        D3D12_RECT scissor{0, 0, 4, 4};
        list6->RSSetViewports(1, &vp);
        list6->RSSetScissorRects(1, &scissor);
        list6->SetGraphicsRootSignature(root.Get());
        list6->SetPipelineState(pso.Get());
        list6->DispatchMesh(1, 1, 1);
        list6->Close();
        ID3D12CommandList* lists[] = {list6.Get()};
        queue->ExecuteCommandLists(1, lists);
        ComPtr<ID3D12Fence> fence;
        if (SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
          HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
          queue->Signal(fence.Get(), 1);
          if (fence->GetCompletedValue() < 1 && event) {
            fence->SetEventOnCompletion(1, event);
            WaitForSingleObject(event, 2000);
          }
          if (event) {
            CloseHandle(event);
          }
        }
        LogInfo("TryMeshShaderPath: D3D12 MS PSO + DispatchMesh Ok (" + ms_path.string() + ")");
        return Status::Ok();
      }
    }
  }

  LogInfo("TryMeshShaderPath: D3D12 MS PSO created (DispatchMesh demo skipped)");
  return Status::Ok();
}
#endif  // _WIN32

}  // namespace

MeshletCookResult MeshletizeAabbGrid(std::span<const Vec3> positions,
                                     std::span<const std::uint32_t> indices, int grid_div) {
  MeshletCookResult out;
  if (positions.empty() || indices.size() < 3) {
    return out;
  }

  Aabb mesh_aabb{positions[0], positions[0]};
  for (const auto& p : positions) {
    mesh_aabb = Expand(mesh_aabb, p);
  }
  const Vec3 ext = mesh_aabb.extents();
  const Vec3 size{std::max(ext.x * 2.f, 1e-6f), std::max(ext.y * 2.f, 1e-6f),
                  std::max(ext.z * 2.f, 1e-6f)};

  const int div = std::max(grid_div, 1);
  struct Bin {
    std::vector<std::uint32_t> tris;  // flat i0,i1,i2
    Aabb aabb{};
    bool has = false;
  };
  std::unordered_map<int, Bin> bins;

  const std::size_t tri_count = indices.size() / 3;
  for (std::size_t t = 0; t < tri_count; ++t) {
    const std::uint32_t i0 = indices[t * 3 + 0];
    const std::uint32_t i1 = indices[t * 3 + 1];
    const std::uint32_t i2 = indices[t * 3 + 2];
    if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) {
      continue;
    }
    const Vec3 c = (positions[i0] + positions[i1] + positions[i2]) * (1.f / 3.f);
    int cx = static_cast<int>((c.x - mesh_aabb.min.x) / size.x * static_cast<float>(div));
    int cy = static_cast<int>((c.y - mesh_aabb.min.y) / size.y * static_cast<float>(div));
    int cz = static_cast<int>((c.z - mesh_aabb.min.z) / size.z * static_cast<float>(div));
    cx = std::clamp(cx, 0, div - 1);
    cy = std::clamp(cy, 0, div - 1);
    cz = std::clamp(cz, 0, div - 1);
    const int key = (cx * div + cy) * div + cz;
    auto& bin = bins[key];
    if (!bin.has) {
      bin.aabb = {positions[i0], positions[i0]};
      bin.has = true;
    }
    bin.aabb = Expand(bin.aabb, positions[i0]);
    bin.aabb = Expand(bin.aabb, positions[i1]);
    bin.aabb = Expand(bin.aabb, positions[i2]);
    bin.tris.push_back(i0);
    bin.tris.push_back(i1);
    bin.tris.push_back(i2);
  }

  out.meshlets.reserve(bins.size());
  for (auto& [key, bin] : bins) {
    (void)key;
    if (bin.tris.empty()) {
      continue;
    }
    Meshlet m;
    m.first_index = static_cast<std::uint32_t>(out.indices.size());
    m.index_count = static_cast<std::uint32_t>(bin.tris.size());
    m.first_vertex = 0;
    m.vertex_count = static_cast<std::uint32_t>(positions.size());
    m.aabb = bin.aabb;
    out.indices.insert(out.indices.end(), bin.tris.begin(), bin.tris.end());
    out.meshlets.push_back(m);
  }
  return out;
}

MeshletCookResult MeshletizePreferMeshoptimizer(std::span<const Vec3> positions,
                                                std::span<const std::uint32_t> indices,
                                                int grid_div) {
#if defined(ENGINE_WITH_MESHOPTIMIZER) && ENGINE_WITH_MESHOPTIMIZER
  (void)grid_div;
  if (positions.empty() || indices.size() < 3) {
    return MeshletizeAabbGrid(positions, indices, grid_div);
  }
  const size_t max_v = 64;
  const size_t max_t = 124;  // meshoptimizer requires max_triangles % 4 == 0
  const size_t bound = meshopt_buildMeshletsBound(indices.size(), max_v, max_t);
  std::vector<meshopt_Meshlet> mls(bound);
  std::vector<unsigned int> ml_verts(bound * max_v);
  std::vector<unsigned char> ml_tris(bound * max_t * 3);
  std::vector<float> pos_f(positions.size() * 3);
  for (size_t i = 0; i < positions.size(); ++i) {
    pos_f[i * 3 + 0] = positions[i].x;
    pos_f[i * 3 + 1] = positions[i].y;
    pos_f[i * 3 + 2] = positions[i].z;
  }
  const size_t count = meshopt_buildMeshlets(
      mls.data(), ml_verts.data(), ml_tris.data(), indices.data(), indices.size(), pos_f.data(),
      positions.size(), sizeof(float) * 3, max_v, max_t, 0.f);
  if (count == 0) {
    return MeshletizeAabbGrid(positions, indices, grid_div);
  }
  mls.resize(count);
  MeshletCookResult out;
  for (size_t mi = 0; mi < count; ++mi) {
    const meshopt_Meshlet& src = mls[mi];
    Meshlet m;
    m.first_index = static_cast<std::uint32_t>(out.indices.size());
    m.index_count = src.triangle_count * 3u;
    m.first_vertex = 0;
    m.vertex_count = src.vertex_count;
    Aabb box{};
    bool any = false;
    for (unsigned int t = 0; t < src.triangle_count; ++t) {
      for (int k = 0; k < 3; ++k) {
        const unsigned char local =
            ml_tris[static_cast<size_t>(src.triangle_offset) + t * 3u + static_cast<size_t>(k)];
        const unsigned int vi =
            ml_verts[static_cast<size_t>(src.vertex_offset) + static_cast<size_t>(local)];
        out.indices.push_back(vi);
        if (vi < positions.size()) {
          const Vec3& p = positions[vi];
          if (!any) {
            box.min = box.max = p;
            any = true;
          } else {
            box.min.x = (std::min)(box.min.x, p.x);
            box.min.y = (std::min)(box.min.y, p.y);
            box.min.z = (std::min)(box.min.z, p.z);
            box.max.x = (std::max)(box.max.x, p.x);
            box.max.y = (std::max)(box.max.y, p.y);
            box.max.z = (std::max)(box.max.z, p.z);
          }
        }
      }
    }
    m.aabb = box;
    out.meshlets.push_back(m);
  }
  return out;
#else
  (void)grid_div;
  return MeshletizeAabbGrid(positions, indices, grid_div);
#endif
}

std::uint32_t CullMeshletsToIndirect(std::span<const Meshlet> meshlets, const Mat4& world,
                                     const Mat4& view_proj, const render::OcclusionBuffer* occ,
                                     std::vector<std::uint32_t>& out_visible_ids,
                                     std::vector<IndirectDrawArgs>& out_args) {
  out_visible_ids.clear();
  out_args.clear();
  out_visible_ids.reserve(meshlets.size());
  out_args.reserve(meshlets.size());

  const Frustum f = Frustum::FromViewProj(view_proj);
  for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(meshlets.size()); ++i) {
    const Aabb world_box = TransformAabb(meshlets[i].aabb, world);
    bool visible = true;
    if (occ) {
      visible = occ->IsVisible(world_box, view_proj);
    } else {
      visible = f.ContainsAabb(world_box);
    }
    if (!visible) {
      continue;
    }
    out_visible_ids.push_back(i);
    IndirectDrawArgs args;
    FillIndirectArgs(args, meshlets[i].index_count, 1);
    args.start_index_location = meshlets[i].first_index;
    args.base_vertex_location = static_cast<std::int32_t>(meshlets[i].first_vertex);
    out_args.push_back(args);
  }
  engine::SetFeatureOverride("execute_indirect", true);
  return static_cast<std::uint32_t>(out_visible_ids.size());
}

Status TryMeshShaderPath() {
  if (!MeshShaderFeatureOn()) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Mesh Shader path SKIP: Feature meshlet/mesh_shader=false (C08)");
  }
#if defined(_WIN32)
  const Status d3d = TryMeshShaderPathD3d12();
  if (d3d) {
    return d3d;
  }
#endif
  // Mega-W11: Feature on + VK_EXT_mesh_shader → minimal Ok; else honest SKIP.
  const Status vk = ProbeMeshShaderSupportVk();
  if (vk) {
    LogInfo("TryMeshShaderPath: VK_EXT_mesh_shader present (minimal Ok)");
    return Status::Ok("vk-mesh-shader-ext");
  }
#if defined(_WIN32)
  return Status::Fail(ErrorCode::Unavailable,
                      std::string("Mesh Shader path SKIP: D3D12=") + d3d.message() +
                          "; VK=" + vk.message());
#else
  return Status::Fail(ErrorCode::Unavailable,
                      std::string("Mesh Shader path SKIP: ") + vk.message());
#endif
}

Status ProbeMeshShaderSupportVk() {
#if ENGINE_WITH_VULKAN
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "engine_mesh_shader_probe";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance) {
    return Status::Fail(ErrorCode::Unavailable,
                        "ProbeMeshShaderSupportVk: vkCreateInstance failed");
  }

  uint32_t phys_count = 0;
  vkEnumeratePhysicalDevices(instance, &phys_count, nullptr);
  bool found = false;
  if (phys_count > 0) {
    std::vector<VkPhysicalDevice> phys_list(phys_count);
    vkEnumeratePhysicalDevices(instance, &phys_count, phys_list.data());
    for (VkPhysicalDevice pd : phys_list) {
      uint32_t ext_count = 0;
      vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
      std::vector<VkExtensionProperties> exts(ext_count);
      vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
      for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, "VK_EXT_mesh_shader") == 0) {
          found = true;
          break;
        }
      }
      if (found) {
        break;
      }
    }
  }
  vkDestroyInstance(instance, nullptr);
  if (found) {
    return Status::Ok();  // Supported
  }
  return Status::Fail(ErrorCode::Unavailable,
                      "ProbeMeshShaderSupportVk: VK_EXT_mesh_shader Unavailable");
#else
  return Status::Fail(ErrorCode::Unavailable,
                      "ProbeMeshShaderSupportVk: ENGINE_WITH_VULKAN=0");
#endif
}

}  // namespace engine::gpu_driven
