#include "d3d12_device_internal.h"

namespace engine::rhi {

Status D3D12Device::UploadRgbaTexture(ComPtr<ID3D12Resource>& tex, UINT srv_slot,
                                      const std::uint8_t* rgba, int width, int height) {
  if (!rgba || width <= 0 || height <= 0) {
    return Status::Fail("Invalid RGBA texture upload");
  }
  if (!shadow_srv_heap_ || !device_ || !queue_) {
    return Status::Fail("Shadow SRV heap missing");
  }

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
  ComPtr<ID3D12Resource> new_tex;
  HRESULT hr = device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&new_tex));
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

  // Dedicated list — never Close/Reset the live frame command_list_ (W20 soft-shadow/GI
  // upload runs before DrawFrame while BeginFrame has already opened the main list).
  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> list;
  hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
  if (FAILED(hr)) {
    return Status::Fail("Create RGBA upload allocator failed");
  }
  hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr,
                                  IID_PPV_ARGS(&list));
  if (FAILED(hr)) {
    return Status::Fail("Create RGBA upload command list failed");
  }

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
  dst_loc.pResource = new_tex.Get();
  dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst_loc.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION src_loc{};
  src_loc.pResource = upload.Get();
  src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src_loc.PlacedFootprint = layout;
  list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = new_tex.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);

  if (FAILED(list->Close())) {
    return Status::Fail("RGBA upload CommandList::Close failed");
  }
  ID3D12CommandList* lists[] = {list.Get()};
  queue_->ExecuteCommandLists(1, lists);
  // Wait for this upload only — do not stamp frame fences (WaitGpu poison).
  WaitGpuSubmitted();

  D3D12_CPU_DESCRIPTOR_HANDLE srv = shadow_srv_heap_->GetCPUDescriptorHandleForHeapStart();
  srv.ptr += static_cast<SIZE_T>(srv_slot) * cbv_srv_uav_descriptor_size_;
  device_->CreateShaderResourceView(new_tex.Get(), nullptr, srv);

  // Retire previous texture only after GPU finished prior work that may sample it.
  tex = std::move(new_tex);
  return Status::Ok();
}

}  // namespace engine::rhi
