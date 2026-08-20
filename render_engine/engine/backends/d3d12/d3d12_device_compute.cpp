#include "d3d12_device_internal.h"
#include "gpu_compute_oneshot_d3d12.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

namespace engine::rhi {

Status D3D12Device::DispatchCompute(const ComputeDispatchDesc& desc) {
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

Status D3D12Device::SetupInstanceCullCompute(const std::filesystem::path& cs_dxil) {
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

Status D3D12Device::DispatchInstanceCull(const Mat4& view_proj, std::uint32_t instance_count,
                            std::uint32_t& out_visible) {
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

Status D3D12Device::SetupLightTileCullCompute(const std::filesystem::path& cs_dxil) {
  tile_cull_ready_ = false;
  tile_cull_gpu_ = false;
  tile_cull_root_.Reset();
  tile_cull_pso_.Reset();
  if (!device_ || cs_dxil.empty()) {
    return Status::Fail(ErrorCode::Unavailable, "SetupLightTileCullCompute: invalid");
  }
  std::ifstream in(cs_dxil, std::ios::binary);
  if (!in) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Light tile CS missing: " + cs_dxil.string());
  }
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    return Status::Fail(ErrorCode::Unavailable, "Light tile CS empty");
  }

  D3D12_ROOT_PARAMETER params[4]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[1].Descriptor.ShaderRegister = 0;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[2].Descriptor.ShaderRegister = 0;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[3].Descriptor.ShaderRegister = 1;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rs{};
  rs.NumParameters = 4;
  rs.pParameters = params;
  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
    tile_cull_ready_ = true;
    LogInfo("D3D12 light tile cull: root serialize failed; CPU Simulate only");
    return Status::Ok("cpu-simulate-only");
  }
  if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                          IID_PPV_ARGS(&tile_cull_root_)))) {
    tile_cull_ready_ = true;
    LogInfo("D3D12 light tile cull: root create failed; CPU Simulate only");
    return Status::Ok("cpu-simulate-only");
  }
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = tile_cull_root_.Get();
  pso.CS = {bytes.data(), bytes.size()};
  if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&tile_cull_pso_)))) {
    tile_cull_root_.Reset();
    tile_cull_ready_ = true;
    LogInfo("D3D12 light tile cull: PSO failed; CPU Simulate only");
    return Status::Ok("cpu-simulate-only");
  }
  tile_cull_ready_ = true;
  tile_cull_gpu_ = true;
  LogInfo("D3D12 light tile cull CS ready (GPU Dispatch; Simulate fallback)");
  return Status::Ok("gpu-cs");
}

Status D3D12Device::DispatchLightTileCull(const Mat4& view_proj, std::span<const Vec3> positions,
                             std::span<const float> ranges, std::array<int, 128>& out_counts,
                             std::array<int, 1024>& out_indices, const Vec3& eye,
                             const Vec3& cam_forward) {
  if (!tile_cull_ready_) {
    out_counts.fill(0);
    out_indices.fill(-1);
    return Status::Fail(ErrorCode::Unavailable, "DispatchLightTileCull: not set up");
  }
  if (tile_cull_gpu_ && tile_cull_pso_ && tile_cull_root_ &&
      TryDispatchLightTileCullGpu(view_proj, positions, ranges, out_counts, out_indices, eye,
                                  cam_forward)) {
    return Status::Ok("gpu-cs");
  }
  engine::render::SimulateLightTileCullCs(view_proj, positions, ranges, out_counts, out_indices,
                                          eye, cam_forward);
  return Status::Ok("cpu-simulate");
}

bool D3D12Device::TryDispatchLightTileCullGpu(const Mat4& view_proj, std::span<const Vec3> positions,
                                 std::span<const float> ranges, std::array<int, 128>& out_counts,
                                 std::array<int, 1024>& out_indices, const Vec3& eye,
                                 const Vec3& cam_forward) {
  if (!device_ || !queue_) {
    return false;
  }
  const UINT n = static_cast<UINT>((std::min)(positions.size(), ranges.size()));
  if (n == 0) {
    out_counts.fill(0);
    out_indices.fill(-1);
    return true;
  }
  struct LightPacked {
    float px, py, pz, range;
  };
  struct TileCullCB {
    float vp[16];
    float eye[3];
    UINT light_count;
    float cam_forward[3];
    float z_near;
    float z_far;
    UINT pad[3];
  };
  std::vector<LightPacked> lights(n);
  for (UINT i = 0; i < n; ++i) {
    lights[i] = {positions[i].x, positions[i].y, positions[i].z, ranges[i]};
  }
  TileCullCB cb{};
  std::memcpy(cb.vp, view_proj.m.data(), sizeof(cb.vp));
  cb.eye[0] = eye.x;
  cb.eye[1] = eye.y;
  cb.eye[2] = eye.z;
  cb.light_count = n;
  cb.cam_forward[0] = cam_forward.x;
  cb.cam_forward[1] = cam_forward.y;
  cb.cam_forward[2] = cam_forward.z;
  cb.z_near = engine::render::kLightZNear;
  cb.z_far = engine::render::kLightZFar;

  auto make_buf = [&](UINT64 size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                      D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& out) -> bool {
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
    return SUCCEEDED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state,
                                                      nullptr, IID_PPV_ARGS(&out)));
  };

  const UINT64 light_bytes = sizeof(LightPacked) * n;
  const UINT64 cb_bytes = (sizeof(TileCullCB) + 255ull) & ~255ull;
  const UINT64 count_bytes = sizeof(int) * 128;
  const UINT64 index_bytes = sizeof(int) * 1024;
  ComPtr<ID3D12Resource> cb_up, light_up, light_def, count_uav, index_uav, count_rb, index_rb;
  if (!make_buf(cb_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_FLAG_NONE, cb_up) ||
      !make_buf(light_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_FLAG_NONE, light_up) ||
      !make_buf(light_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_FLAG_NONE, light_def) ||
      !make_buf(count_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, count_uav) ||
      !make_buf(index_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, index_uav) ||
      !make_buf(count_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_FLAG_NONE, count_rb) ||
      !make_buf(index_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_FLAG_NONE, index_rb)) {
    return false;
  }
  {
    void* mapped = nullptr;
    if (FAILED(cb_up->Map(0, nullptr, &mapped)) || !mapped) {
      return false;
    }
    std::memcpy(mapped, &cb, sizeof(cb));
    cb_up->Unmap(0, nullptr);
  }
  {
    void* mapped = nullptr;
    if (FAILED(light_up->Map(0, nullptr, &mapped)) || !mapped) {
      return false;
    }
    std::memcpy(mapped, lights.data(), static_cast<size_t>(light_bytes));
    light_up->Unmap(0, nullptr);
  }

  gpu_compute::D3D12ComputeOneShot oneshot(device_.Get(), queue_.Get());
  const bool recorded = oneshot.Run([&](ID3D12GraphicsCommandList* list) {
  list->CopyBufferRegion(light_def.Get(), 0, light_up.Get(), 0, light_bytes);
  D3D12_RESOURCE_BARRIER bar{};
  bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  bar.Transition.pResource = light_def.Get();
  bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  bar.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &bar);

  list->SetPipelineState(tile_cull_pso_.Get());
  list->SetComputeRootSignature(tile_cull_root_.Get());
  list->SetComputeRootConstantBufferView(0, cb_up->GetGPUVirtualAddress());
  list->SetComputeRootShaderResourceView(1, light_def->GetGPUVirtualAddress());
  list->SetComputeRootUnorderedAccessView(2, count_uav->GetGPUVirtualAddress());
  list->SetComputeRootUnorderedAccessView(3, index_uav->GetGPUVirtualAddress());
  list->Dispatch(8, 4, 4);

  D3D12_RESOURCE_BARRIER uavs[2]{};
  uavs[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavs[0].UAV.pResource = count_uav.Get();
  uavs[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavs[1].UAV.pResource = index_uav.Get();
  list->ResourceBarrier(2, uavs);

  auto to_copy = [&](ID3D12Resource* uav) {
    D3D12_RESOURCE_BARRIER t{};
    t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    t.Transition.pResource = uav;
    t.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    t.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    t.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &t);
  };
  to_copy(count_uav.Get());
  to_copy(index_uav.Get());
  list->CopyBufferRegion(count_rb.Get(), 0, count_uav.Get(), 0, count_bytes);
  list->CopyBufferRegion(index_rb.Get(), 0, index_uav.Get(), 0, index_bytes);
  });
  if (!recorded) {
    return false;
  }

  {
    void* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(count_bytes)};
    if (SUCCEEDED(count_rb->Map(0, &range, &mapped)) && mapped) {
      std::memcpy(out_counts.data(), mapped, count_bytes);
      count_rb->Unmap(0, nullptr);
    } else {
      return false;
    }
  }
  {
    void* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(index_bytes)};
    if (SUCCEEDED(index_rb->Map(0, &range, &mapped)) && mapped) {
      std::memcpy(out_indices.data(), mapped, index_bytes);
      index_rb->Unmap(0, nullptr);
    } else {
      return false;
    }
  }
  ++compute_dispatches_;
  return true;
}

Status D3D12Device::ProbeBindlessMinimalPath(std::uint32_t srv_heap_slot) {
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

float D3D12Device::BindlessAlbedoHeapPad(float tex_slot) const {
  if (!bindless_capable_ || gpu_headless_ || !engine::QueryFeature("bindless_hot_path")) {
    return -1.f;
  }
  return tex_slot > 0.5f ? 4.f : 1.f;
}

Status D3D12Device::UploadIndirectIndexedArgs(std::span<const std::uint32_t> raw_u32) {
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

Status D3D12Device::ExecuteIndirectIndexed(std::uint32_t draw_count) {
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

Status D3D12Device::TryMeshShaderHotPath() {
  if (!engine::QueryFeature("meshlet") && !engine::QueryFeature("mesh_shader")) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryMeshShaderHotPath SKIP: Feature meshlet/mesh_shader=false");
  }
  if (!device_) {
    return Status::Fail(ErrorCode::Unavailable, "TryMeshShaderHotPath SKIP: no device");
  }

  D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7{};
  const HRESULT hr_opts =
      device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7));
  if (FAILED(hr_opts) || opts7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryMeshShaderHotPath SKIP: D3D12 MeshShaderTier not supported");
  }

  if (mesh_shader_pso_) {
    return Status::Ok("d3d12-ms-hotpath-cached");
  }

  auto resolve = [](const char* filename) -> std::filesystem::path {
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
  };
  const auto ms_path = resolve("meshlet_ms.ms.cso");
  const auto ps_path = resolve("meshlet_ms.ps.cso");
  if (ms_path.empty() || ps_path.empty()) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryMeshShaderHotPath SKIP: meshlet_ms.cso not found");
  }
  auto load = [](const std::filesystem::path& p, std::vector<char>& out) -> bool {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
      return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
  };
  std::vector<char> ms_bytes;
  std::vector<char> ps_bytes;
  if (!load(ms_path, ms_bytes) || !load(ps_path, ps_bytes)) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryMeshShaderHotPath SKIP: failed to read meshlet_ms DXIL");
  }

  D3D12_ROOT_SIGNATURE_DESC rs_desc{};
  rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  if (FAILED(D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryMeshShaderHotPath SKIP: serialize root failed");
  }
  ComPtr<ID3D12RootSignature> root;
  if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                          IID_PPV_ARGS(&root)))) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryMeshShaderHotPath SKIP: CreateRootSignature failed");
  }

  ComPtr<ID3D12Device2> device2;
  if (FAILED(device_.As(&device2)) || !device2) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryMeshShaderHotPath SKIP: ID3D12Device2 unavailable");
  }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
  struct alignas(void*) MeshPsoStream {
    struct alignas(void*) {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type =
          D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
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

  MeshPsoStream stream{};
  stream.root_signature.pRootSignature = root.Get();
  stream.ms.bytecode = {ms_bytes.data(), ms_bytes.size()};
  stream.ps.bytecode = {ps_bytes.data(), ps_bytes.size()};
  stream.rtv_formats.rt_formats.NumRenderTargets = 1;
  stream.rtv_formats.rt_formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

  D3D12_PIPELINE_STATE_STREAM_DESC stream_desc{};
  stream_desc.SizeInBytes = sizeof(stream);
  stream_desc.pPipelineStateSubobjectStream = &stream;

  ComPtr<ID3D12PipelineState> pso;
  const HRESULT hr_pso = device2->CreatePipelineState(&stream_desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr_pso) || !pso) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "TryMeshShaderHotPath SKIP: CreatePipelineState hr=0x%08X",
                  static_cast<unsigned>(hr_pso));
    return Status::Fail(ErrorCode::Unavailable, buf);
  }

  mesh_shader_root_ = std::move(root);
  mesh_shader_pso_ = std::move(pso);
  // Cache PSO only — avoid startup DispatchMesh/WaitGpu on the live queue (main list hygiene).
  LogInfo("TryMeshShaderHotPath: D3D12 MS PSO cached");
  return Status::Ok("d3d12-ms-hotpath-pso");
}

}  // namespace engine::rhi
