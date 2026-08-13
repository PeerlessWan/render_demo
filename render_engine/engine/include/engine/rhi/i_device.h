#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace engine::rhi {

struct DeviceDesc {
  void* native_window = nullptr;  // HWND on Win32; null for headless
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool headless = false;
};

struct SimpleMeshShaders {
  std::filesystem::path vs_dxil;
  std::filesystem::path ps_dxil;
};

struct ComputeDispatchDesc {
  std::uint32_t groups_x = 1;
  std::uint32_t groups_y = 1;
  std::uint32_t groups_z = 1;
};

class IDevice {
 public:
  virtual ~IDevice() = default;

  [[nodiscard]] virtual bool is_headless() const { return false; }

  virtual Status BeginFrame() = 0;
  virtual Status Clear(const ColorRgba& color) = 0;
  virtual Status DrawSimpleMesh() = 0;
  virtual Status Present() = 0;
  virtual Status Resize(std::uint32_t width, std::uint32_t height) = 0;
  virtual Status SetupSimpleMesh(const SimpleMeshShaders& shaders) = 0;

  // M3 deepen: compute + readback contracts (headless implements CPU stubs).
  virtual Status DispatchCompute(const ComputeDispatchDesc& desc) = 0;
  virtual Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) = 0;
};

Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc& desc);
Result<std::unique_ptr<IDevice>> CreateHeadlessDevice(const DeviceDesc& desc);

}  // namespace engine::rhi
