#include "gpu_compute_oneshot_d3d12.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace engine::rhi::gpu_compute {

D3D12ComputeOneShot::D3D12ComputeOneShot(ID3D12Device* device, ID3D12CommandQueue* queue)
    : device_(device), queue_(queue) {}

bool D3D12ComputeOneShot::Run(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
  if (!device_ || !queue_ || !record) {
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
  if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&alloc))) ||
      FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr,
                                       IID_PPV_ARGS(&list)))) {
    return false;
  }
  record(list.Get());
  if (FAILED(list->Close())) {
    return false;
  }
  ID3D12CommandList* lists[] = {list.Get()};
  queue_->ExecuteCommandLists(1, lists);

  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
    return false;
  }
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) {
    return false;
  }
  const UINT64 value = 1;
  if (FAILED(queue_->Signal(fence.Get(), value)) ||
      FAILED(fence->SetEventOnCompletion(value, event))) {
    CloseHandle(event);
    return false;
  }
  WaitForSingleObject(event, INFINITE);
  CloseHandle(event);
  return true;
}

}  // namespace engine::rhi::gpu_compute
