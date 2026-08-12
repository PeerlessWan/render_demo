#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace engine::rhi {

struct DeviceDesc {
  void* native_window = nullptr;  // HWND on Win32
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct SimpleMeshShaders {
  std::filesystem::path vs_dxil;
  std::filesystem::path ps_dxil;
};

class IDevice {
 public:
  virtual ~IDevice() = default;

  virtual Status BeginFrame() = 0;
  virtual Status Clear(const ColorRgba& color) = 0;
  // M2: draw the built-in textured triangle (must call SetupSimpleMesh first).
  virtual Status DrawSimpleMesh() = 0;
  virtual Status Present() = 0;
  virtual Status Resize(std::uint32_t width, std::uint32_t height) = 0;

  // Compile-time DXIL blobs produced by tools/shader_compile (DXC).
  virtual Status SetupSimpleMesh(const SimpleMeshShaders& shaders) = 0;
};

Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc& desc);

}  // namespace engine::rhi
