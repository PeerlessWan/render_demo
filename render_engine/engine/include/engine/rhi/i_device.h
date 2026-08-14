#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/rhi/submit_config.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace engine::rhi {

struct DeviceDesc {
  void* native_window = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool headless = false;
  // True GPU without HWND (D3D12 offscreen RTs). Mutually preferred over CPU HeadlessDevice.
  bool gpu_headless = false;
  // Request display HDR10 path when swapchain exists (ignored for offscreen/gpu_headless).
  bool enable_hdr_output = false;
};

struct SimpleMeshShaders {
  std::filesystem::path vs_dxil;
  std::filesystem::path ps_dxil;
};

struct LitMeshShaders {
  std::filesystem::path vs_dxil;
  std::filesystem::path ps_dxil;
  std::filesystem::path shadow_vs_dxil;
  std::filesystem::path shadow_ps_dxil;
  std::filesystem::path quad_vs_dxil;
  std::filesystem::path quad_ps_dxil;
  std::filesystem::path debug_vs_dxil;
  std::filesystem::path debug_ps_dxil;
};

struct PostShaders {
  std::filesystem::path vs_dxil;
  std::filesystem::path ps_dxil;
};

struct PostResolveDesc {
  Mat4 inv_view_proj = Mat4::Identity();
  Mat4 view_proj = Mat4::Identity();
  Vec3 eye{0, 1, 4};
  bool enable_ssao = false;
  bool enable_taa = false;
  float ssao_radius = 12.f;
  float ssao_intensity = 0.85f;
  float taa_blend = 0.25f;
  float exposure = 1.15f;
  bool enable_tonemap = true;
  int tonemap_mode = 2;  // 0=none 1=reinhard 2=ACES
  bool enable_auto_exposure = true;
  float auto_exposure_key = 0.18f;
  bool enable_bloom = false;
  float bloom_threshold = 0.85f;
  float bloom_intensity = 0.4f;
  bool enable_fog = false;
  float fog_density = 0.02f;
  float fog_start = 12.f;
  Vec3 fog_color{0.62f, 0.70f, 0.78f};
  bool enable_ssr = false;
  float ssr_intensity = 0.55f;
  float ssr_thickness = 0.015f;
  bool enable_dof = false;
  float dof_focus = 8.f;
  float dof_scale = 0.08f;
  bool enable_motion_blur = false;
  float motion_blur_strength = 0.35f;
  Mat4 prev_view_proj = Mat4::Identity();
  float jitter_x = 0.f;
  float jitter_y = 0.f;

  [[nodiscard]] bool NeedsResolve() const {
    return enable_ssao || enable_taa || enable_tonemap || enable_auto_exposure || enable_bloom ||
           enable_fog || enable_ssr || enable_dof || enable_motion_blur ||
           (exposure > 1.0001f || exposure < 0.9999f);
  }
};

struct LitVertex {
  float px = 0, py = 0, pz = 0;
  float nx = 0, ny = 1, nz = 0;
  float u = 0, v = 0;
};

struct ComputeDispatchDesc {
  std::uint32_t groups_x = 1;
  std::uint32_t groups_y = 1;
  std::uint32_t groups_z = 1;
};

struct FrameLighting {
  Mat4 view_proj = Mat4::Identity();
  Mat4 prev_view_proj = Mat4::Identity();  // TAA / motion reprojection
  Mat4 light_view_proj = Mat4::Identity();  // cascade 0 (compat)
  std::array<Mat4, 4> cascade_view_proj{};
  std::array<float, 4> cascade_splits{};
  int cascade_count = 1;
  int cascade_tiles_per_row = 1;
  Vec3 sun_direction{0.3f, -1.f, 0.2f};
  float sun_intensity = 2.5f;
  ColorRgba ambient{0.08f, 0.09f, 0.11f, 1.f};
  ColorRgba sun_color{1.f, 0.96f, 0.9f, 1.f};
  Vec3 eye{0, 1, 4};
  Vec3 camera_forward{0, 0, -1};
  float shadow_bias = 0.0015f;
  float specular_power = 64.f;
  float jitter_x = 0.f;  // NDC sub-pixel offset when TAA on
  float jitter_y = 0.f;
  bool enable_shadows = true;
  bool enable_ssao = false;
  bool enable_taa = false;
  bool enable_reflection_probe = false;
  float reflection_intensity = 0.45f;
  // Up to 4 local (point) lights.
  int local_light_count = 0;
  std::array<Vec3, 4> local_pos{};
  std::array<float, 4> local_range{};
  std::array<ColorRgba, 4> local_color{};
  std::array<float, 4> local_intensity{};
  // First casting local lights: cubemap face atlas (up to 2 lights × 6 faces = 12 tiles).
  Mat4 local_shadow_vp = Mat4::Identity();  // tile 0 compat (+X of light 0)
  std::array<Mat4, 12> local_shadow_vps{};
  int local_shadow_count = 0;       // number of lights casting cube shadows
  int local_shadow_tile_count = 0;  // local_shadow_count * 6
  int local_shadow_tiles_per_row = 4;
  bool enable_local_shadow = false;
  float local_shadow_bias = 0.002f;
};

struct LitDrawItem {
  Mat4 world = Mat4::Identity();
  ColorRgba color{0.75f, 0.75f, 0.78f, 1.f};
  float metallic = 0.05f;
  float roughness = 0.45f;
  bool use_albedo = true;
  bool use_orm = false;  // sample ORM map (R=AO, G=roughness, B=metallic)
  bool transparent = false;
  int mesh_slot = 0;   // 0 = unit cube; custom via UploadLitGeometry
  int tex_slot = 0;    // 0 = primary albedo/ORM; 1 = secondary
  float uv_scale = 1.f;
};

struct GpuPassTiming {
  std::string name;
  double ms = 0.0;
};

struct ScreenQuad {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // pixel coords
  ColorRgba color{1, 1, 1, 1};
};

// Textured UI triangles (ImGui font atlas path). Positions are in pixels.
struct UiVertex {
  float x = 0, y = 0;
  float u = 0, v = 0;
  float r = 1, g = 1, b = 1, a = 1;
};

struct UiDrawCmd {
  std::uint32_t index_offset = 0;
  std::uint32_t index_count = 0;
  float clip_x0 = 0, clip_y0 = 0, clip_x1 = 0, clip_y1 = 0;
};

struct DebugLineVertex {
  float x = 0, y = 0, z = 0;
  float r = 1, g = 1, b = 1, a = 1;
};

class IDevice {
 public:
  virtual ~IDevice() = default;

  [[nodiscard]] virtual bool is_headless() const { return false; }
  [[nodiscard]] virtual std::uint32_t width() const = 0;
  [[nodiscard]] virtual std::uint32_t height() const = 0;

  virtual Status BeginFrame() = 0;
  virtual Status Clear(const ColorRgba& color) = 0;
  virtual Status DrawSimpleMesh() = 0;
  virtual Status Present() = 0;
  virtual Status Resize(std::uint32_t width, std::uint32_t height) = 0;
  virtual Status SetupSimpleMesh(const SimpleMeshShaders& shaders) = 0;

  virtual Status SetupLitMesh(const LitMeshShaders& shaders) = 0;
  virtual Status SetFrameLighting(const FrameLighting& lighting) = 0;
  virtual Status BeginShadowPass() = 0;
  // Select cascade tile + light matrix (call after BeginShadowPass / SetFrameLighting).
  virtual Status BindShadowCascade(int cascade_index) = 0;
  virtual Status DrawShadowCubes(std::span<const LitDrawItem> items) = 0;
  virtual Status EndShadowPass() = 0;
  // Perspective depth atlas for local lights (spot-like point shadow).
  virtual Status BeginLocalShadowPass() = 0;
  virtual Status BindLocalShadowTile(int tile_index) = 0;
  virtual Status EndLocalShadowPass() = 0;
  virtual Status DrawLitCube(const LitDrawItem& item) = 0;
  virtual Status DrawLitCubes(std::span<const LitDrawItem> items) = 0;
  // Alpha-blend lit draws (depth write off). Caller sorts back-to-front.
  virtual Status DrawTransparentLitCubes(std::span<const LitDrawItem> items) = 0;

  // Replace default procedural albedo / ORM (RGBA8, row-major). Call after SetupLitMesh.
  // slot: 0 = primary (t1/t3), 1 = secondary (t4/t5).
  virtual Status UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height,
                                     int slot = 0) = 0;
  virtual Status UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height,
                                  int slot = 0) = 0;
  // Replace/add lit mesh geometry (slot 0 reserved for unit cube after SetupLitMesh).
  virtual Status UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                                   std::span<const std::uint32_t> indices) = 0;

  // Depth SSAO + history TAA resolve (after opaque, before UI).
  virtual Status SetupPostMesh(const PostShaders& shaders) = 0;
  virtual Status ResolvePostEffects(const PostResolveDesc& desc) = 0;

  // Optional GPU pass timestamps (D3D12). No-ops on backends without support.
  virtual void GpuPassBegin(const char* /*name*/) {}
  virtual void GpuPassEnd() {}
  [[nodiscard]] virtual std::vector<GpuPassTiming> LastGpuPassTimings() const { return {}; }

  // Screen-space UI/2D quads (NDC via pixel→viewport).
  virtual Status DrawScreenQuads(std::span<const ScreenQuad> quads) = 0;

  // World-space debug lines (grid/axes). Call after SetupLitMesh with debug shaders set.
  virtual Status DrawDebugLines(std::span<const DebugLineVertex> lines_as_segments) = 0;

  // Immediate UI (font atlas + indexed textured triangles).
  virtual Status SetupUiMesh(const SimpleMeshShaders& shaders) = 0;
  virtual Status UploadUiFontAtlas(const std::uint8_t* rgba, int width, int height) = 0;
  virtual Status DrawUiMesh(std::span<const UiVertex> vertices,
                            std::span<const std::uint16_t> indices,
                            std::span<const UiDrawCmd> commands) = 0;

  virtual Status DispatchCompute(const ComputeDispatchDesc& desc) = 0;
  virtual Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) = 0;

  // M14: parallel submit preference (validated; backends may still fall back to single-thread).
  virtual Status SetSubmitConfig(const SubmitConfig& cfg) { return ValidateSubmitConfig(cfg); }

  // M13: upload 6 RGBA8 faces (face-major, each face = size*size*4) for specular reflections.
  virtual Status UploadReflectionCubemap(const std::uint8_t* /*rgba_faces*/, int /*face_size*/) {
    return Status::Ok();
  }
};

Result<std::unique_ptr<IDevice>> CreateD3D12Device(const DeviceDesc& desc);
Result<std::unique_ptr<IDevice>> CreateHeadlessDevice(const DeviceDesc& desc);
// Real Vulkan path when ENGINE_WITH_VULKAN=1 (Win32 clear + optional lit cube SPIR-V).
Result<std::unique_ptr<IDevice>> CreateVulkanDevice(const DeviceDesc& desc);

}  // namespace engine::rhi
