#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>

namespace engine::rhi::gpu_compute {

// W20: one-shot DIRECT command list + fence wait for compute/readback demos.
// New device CS paths (tile cull, VT feedback, …) should prefer this helper.
class D3D12ComputeOneShot {
 public:
  explicit D3D12ComputeOneShot(ID3D12Device* device, ID3D12CommandQueue* queue);

  [[nodiscard]] bool valid() const { return device_ && queue_; }

  bool Run(const std::function<void(ID3D12GraphicsCommandList*)>& record);

 private:
  ID3D12Device* device_ = nullptr;
  ID3D12CommandQueue* queue_ = nullptr;
};

}  // namespace engine::rhi::gpu_compute
