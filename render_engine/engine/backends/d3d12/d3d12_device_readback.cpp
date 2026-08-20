#include "d3d12_device_internal.h"

namespace engine::rhi {

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
