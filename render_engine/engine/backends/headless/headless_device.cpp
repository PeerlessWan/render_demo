#include "engine/rhi/i_device.h"

#include "engine/core/log.h"

#include <algorithm>
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
  [[nodiscard]] DeviceApiKind api_kind() const override { return DeviceApiKind::Headless; }
  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }

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

  Status BeginShadowPass() override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    shadow_active_ = true;
    ++shadow_passes_;
    return Status::Ok();
  }

  Status BindShadowCascade(int cascade_index) override {
    if (!lit_ready_ || !shadow_active_) {
      return Status::Fail("BeginShadowPass not active");
    }
    if (cascade_index < 0 || cascade_index >= std::max(1, lighting_.cascade_count)) {
      return Status::Fail("Invalid shadow cascade index");
    }
    bound_cascade_ = cascade_index;
    ++cascade_binds_;
    return Status::Ok();
  }

  Status DrawShadowCubes(std::span<const LitDrawItem> items) override {
    if (!lit_ready_ || (!shadow_active_ && !local_shadow_active_)) {
      return Status::Fail("BeginShadowPass/BeginLocalShadowPass not active");
    }
    shadow_draws_ += static_cast<std::uint32_t>(items.size());
    return Status::Ok();
  }

  Status EndShadowPass() override {
    if (!shadow_active_) {
      return Status::Fail("BeginShadowPass not active");
    }
    shadow_active_ = false;
    return Status::Ok();
  }

  Status BeginLocalShadowPass() override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    local_shadow_active_ = true;
    ++local_shadow_passes_;
    return Status::Ok();
  }

  Status BindLocalShadowTile(int tile_index) override {
    if (!lit_ready_ || !local_shadow_active_) {
      return Status::Fail("BeginLocalShadowPass not active");
    }
    if (tile_index < 0 ||
        tile_index >= std::max(1, lighting_.local_shadow_tile_count > 0
                                      ? lighting_.local_shadow_tile_count
                                      : lighting_.local_shadow_count)) {
      return Status::Fail("Invalid local shadow tile index");
    }
    bound_local_tile_ = tile_index;
    ++local_tile_binds_;
    return Status::Ok();
  }

  Status EndLocalShadowPass() override {
    if (!local_shadow_active_) {
      return Status::Fail("BeginLocalShadowPass not active");
    }
    local_shadow_active_ = false;
    return Status::Ok();
  }

  Status SetupPostMesh(const PostShaders&) override {
    post_ready_ = true;
    return Status::Ok();
  }

  Status ResolvePostEffects(const PostResolveDesc& desc) override {
    if (!post_ready_) {
      return Status::Fail("SetupPostMesh not called");
    }
    if (!desc.NeedsResolve()) {
      return Status::Ok();
    }
    ++post_resolves_;
    return Status::Ok();
  }

  Status DrawLitCube(const LitDrawItem& item) override {
    return DrawLitCubes(std::span<const LitDrawItem>(&item, 1));
  }

  Status DrawLitCubes(std::span<const LitDrawItem> items) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    // Metallic/roughness accepted via LitDrawItem; headless only counts draws.
    lit_draws_ += static_cast<std::uint32_t>(items.size());
    return Status::Ok();
  }

  Status DrawTransparentLitCubes(std::span<const LitDrawItem> items) override {
    return DrawLitCubes(items);
  }

  Status UploadLitAlbedoRgba(const std::uint8_t*, int width, int height, int) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (width <= 0 || height <= 0) {
      return Status::Fail("Invalid albedo size");
    }
    ++albedo_uploads_;
    return Status::Ok();
  }

  Status UploadLitOrmRgba(const std::uint8_t*, int width, int height, int) override {
    if (!lit_ready_) {
      return Status::Fail("SetupLitMesh not called");
    }
    if (width <= 0 || height <= 0) {
      return Status::Fail("Invalid ORM size");
    }
    ++orm_uploads_;
    return Status::Ok();
  }

  Status UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                           std::span<const std::uint32_t> indices) override {
    if (mesh_slot < 0 || vertices.empty() || indices.empty()) {
      return Status::Fail("Invalid lit geometry");
    }
    return Status::Ok();
  }

  Status DrawScreenQuads(std::span<const ScreenQuad> quads) override {
    screen_quad_draws_ += static_cast<std::uint32_t>(quads.size());
    return Status::Ok();
  }

  Status DrawDebugLines(std::span<const DebugLineVertex> lines) override {
    (void)lines;
    return Status::Ok();
  }

  Status SetupUiMesh(const SimpleMeshShaders&) override {
    ui_ready_ = true;
    return Status::Ok();
  }

  Status UploadUiFontAtlas(const std::uint8_t*, int width, int height) override {
    if (!ui_ready_) {
      return Status::Fail("SetupUiMesh not called");
    }
    if (width <= 0 || height <= 0) {
      return Status::Fail("Invalid font atlas size");
    }
    font_atlas_w_ = width;
    font_atlas_h_ = height;
    return Status::Ok();
  }

  Status DrawUiMesh(std::span<const UiVertex> vertices, std::span<const std::uint16_t> indices,
                    std::span<const UiDrawCmd> commands) override {
    if (!ui_ready_) {
      return Status::Fail("SetupUiMesh not called");
    }
    ui_verts_ += static_cast<std::uint32_t>(vertices.size());
    ui_indices_ += static_cast<std::uint32_t>(indices.size());
    ui_cmds_ += static_cast<std::uint32_t>(commands.size());
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
  bool ui_ready_ = false;
  bool post_ready_ = false;
  bool shadow_active_ = false;
  bool local_shadow_active_ = false;
  int bound_cascade_ = 0;
  int bound_local_tile_ = 0;
  int font_atlas_w_ = 0;
  int font_atlas_h_ = 0;
  std::uint32_t frames_begun_ = 0;
  std::uint32_t clears_ = 0;
  std::uint32_t draws_ = 0;
  std::uint32_t lit_draws_ = 0;
  std::uint32_t shadow_draws_ = 0;
  std::uint32_t shadow_passes_ = 0;
  std::uint32_t local_shadow_passes_ = 0;
  std::uint32_t cascade_binds_ = 0;
  std::uint32_t local_tile_binds_ = 0;
  std::uint32_t post_resolves_ = 0;
  std::uint32_t screen_quad_draws_ = 0;
  std::uint32_t albedo_uploads_ = 0;
  std::uint32_t orm_uploads_ = 0;
  std::uint32_t ui_verts_ = 0;
  std::uint32_t ui_indices_ = 0;
  std::uint32_t ui_cmds_ = 0;
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
