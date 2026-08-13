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
  void* native_window = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool headless = false;
};

struct SimpleMeshShaders {
  std::filesystem::path vs_dxil;
  std::filesystem::path ps_dxil;
};

struct LitMeshShaders {
  std::filesystem::path vs_dxil;
  std::filesystem::path ps_dxil;
};

struct ComputeDispatchDesc {
  std::uint32_t groups_x = 1;
  std::uint32_t groups_y = 1;
  std::uint32_t groups_z = 1;
};

struct FrameLighting {
  Mat4 view_proj = Mat4::Identity();
  Vec3 sun_direction{0.3f, -1.f, 0.2f};
  float sun_intensity = 2.5f;
  ColorRgba ambient{0.08f, 0.09f, 0.11f, 1.f};
  ColorRgba sun_color{1.f, 0.96f, 0.9f, 1.f};
};

struct LitDrawItem {
  Mat4 world = Mat4::Identity();
  ColorRgba color{0.75f, 0.75f, 0.78f, 1.f};
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

  // Usable lit path: directional light + unit cube instances.
  virtual Status SetupLitMesh(const LitMeshShaders& shaders) = 0;
  virtual Status SetFrameLighting(const FrameLighting& lighting) = 0;
  virtual Status DrawLitCube(const LitDrawItem& item) = 0;
  virtual Status DrawLitCubes(std::span<const LitDrawItem> items) = 0;

  virtual Status DispatchCompute(const ComputeDispatchDesc& desc) = 0;
  virtual Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) = 0;
};

Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc& desc);
Result<std::unique_ptr<IDevice>> CreateHeadlessDevice(const DeviceDesc& desc);

}  // namespace engine::rhi
