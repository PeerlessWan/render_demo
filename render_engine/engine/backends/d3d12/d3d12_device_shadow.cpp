#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::BeginShadowPass() {
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

Status D3D12Device::BindShadowCascade(int cascade_index) {
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

Status D3D12Device::DrawShadowCubes(std::span<const LitDrawItem> items) {
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

Status D3D12Device::EndShadowPass() {
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

Status D3D12Device::BeginLocalShadowPass() {
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

Status D3D12Device::BindLocalShadowTile(int tile_index) {
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

Status D3D12Device::EndLocalShadowPass() {
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

UINT64 D3D12Device::ShadowVpCbOffset(int slot) const {
    const int s = (std::max)(0, (std::min)(slot, static_cast<int>(kShadowVpSlots) - 1));
    return (static_cast<UINT64>(frame_index_) * kShadowVpSlots + static_cast<UINT64>(s)) * 256ull;
}

Status D3D12Device::CreateShadowMap() {
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
    srv_heap.NumDescriptors = 16;  // t0..t8 + t10..t12 + bindless-friendly room
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

Status D3D12Device::CreateLocalShadowMap() {
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

}  // namespace engine::rhi
