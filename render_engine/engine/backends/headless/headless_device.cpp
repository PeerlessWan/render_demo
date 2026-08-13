#include "engine/rhi/i_device.h"

#include "engine/core/log.h"

#include <cstring>

namespace engine::rhi {
namespace {

class HeadlessDevice final : public IDevice {
 public:
  explicit HeadlessDevice(DeviceDesc desc) : desc_(desc) {
    width_ = desc.width;
    height_ = desc.height;
    clear_ = {0.05f, 0.07f, 0.1f, 1.f};
  }

  [[nodiscard]] bool is_headless() const override { return true; }

  Status BeginFrame() override {
    ++frames_begun_;
    return Status::Ok();
  }

  Status Clear(const ColorRgba& color) override {
    clear_ = color;
    ++clears_;
    return Status::Ok();
  }

  Status DrawSimpleMesh() override {
    if (!mesh_ready_) {
      return Status::Fail("SetupSimpleMesh not called");
    }
    ++draws_;
    return Status::Ok();
  }

  Status Present() override {
    ++presents_;
    return Status::Ok();
  }

  Status Resize(std::uint32_t width, std::uint32_t height) override {
    if (width == 0 || height == 0) {
      return Status::Ok();
    }
    width_ = width;
    height_ = height;
    return Status::Ok();
  }

  Status SetupSimpleMesh(const SimpleMeshShaders&) override {
    mesh_ready_ = true;
    return Status::Ok();
  }

  Status SetupLitMesh(const LitMeshShaders&) override {
    lit_ready_ = true;
    return Status::Ok();
  }

  Status SetFrameLighting(const FrameLighting& lighting) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    lighting_ = lighting;
    return Status::Ok();
  }

  Status DrawLitCube(const LitDrawItem& item) override {
    return DrawLitCubes(std::span<const LitDrawItem>(&item, 1));
  }

  Status DrawLitCubes(std::span<const LitDrawItem> items) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    lit_draws_ += static_cast<std::uint32_t>(items.size());
    return Status::Ok();
  }

  Status DispatchCompute(const ComputeDispatchDesc& desc) override {
    if (desc.groups_x == 0 || desc.groups_y == 0 || desc.groups_z == 0) {
      return Status::Fail(ErrorCode::InvalidArgument, "compute groups must be > 0");
    }
    last_dispatch_ = desc;
    ++compute_dispatches_;
    return Status::Ok();
  }

  Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override {
    w = static_cast<int>(width_);
    h = static_cast<int>(height_);
    out_rgba.resize(static_cast<std::size_t>(w * h * 4));
    const auto r = static_cast<std::uint8_t>(clear_.r * 255.f);
    const auto g = static_cast<std::uint8_t>(clear_.g * 255.f);
    const auto b = static_cast<std::uint8_t>(clear_.b * 255.f);
    const auto a = static_cast<std::uint8_t>(clear_.a * 255.f);
    for (int i = 0; i < w * h; ++i) {
      out_rgba[static_cast<std::size_t>(i * 4 + 0)] = r;
      out_rgba[static_cast<std::size_t>(i * 4 + 1)] = g;
      out_rgba[static_cast<std::size_t>(i * 4 + 2)] = b;
      out_rgba[static_cast<std::size_t>(i * 4 + 3)] = a;
    }
    return Status::Ok();
  }

 private:
  DeviceDesc desc_{};
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  ColorRgba clear_{};
  bool mesh_ready_ = false;
  bool lit_ready_ = false;
  std::uint32_t frames_begun_ = 0;
  std::uint32_t clears_ = 0;
  std::uint32_t draws_ = 0;
  std::uint32_t lit_draws_ = 0;
  std::uint32_t presents_ = 0;
  std::uint32_t compute_dispatches_ = 0;
  ComputeDispatchDesc last_dispatch_{};
  FrameLighting lighting_{};
};

}  // namespace

Result<std::unique_ptr<IDevice>> CreateHeadlessDevice(const DeviceDesc& desc) {
  if (desc.width == 0 || desc.height == 0) {
    return Result<std::unique_ptr<IDevice>>::Fail("Invalid headless DeviceDesc");
  }
  LogInfo("Headless RHI device created");
  return Result<std::unique_ptr<IDevice>>::Ok(std::make_unique<HeadlessDevice>(desc));
}

}  // namespace engine::rhi
