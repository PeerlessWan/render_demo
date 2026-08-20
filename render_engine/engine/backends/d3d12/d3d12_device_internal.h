#pragma once

#include "engine/rhi/i_device.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/render/local_lights.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace engine::rhi {


namespace device_detail {

constexpr std::uint32_t kFrameCount = 2;
constexpr UINT kMaxGpuPasses = 32;
constexpr UINT kMaxTimestampQueries = 64;  // 32 begin/end pairs

inline std::string HrToString(HRESULT hr) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(hr));
  return buf;
}

inline Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return Result<std::vector<std::uint8_t>>::Fail("Cannot open shader: " + path.string());
  }
  const auto size = in.tellg();
  if (size <= 0) {
    return Result<std::vector<std::uint8_t>>::Fail("Empty shader: " + path.string());
  }
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(data.data()), size);
  return Result<std::vector<std::uint8_t>>::Ok(std::move(data));
}

struct Vertex {
  float px, py, pz;
  float r, g, b;
  float u, v;
};

}  // namespace device_detail

using namespace device_detail;

class D3D12Device final : public IDevice {
 public:
  Status Init(const DeviceDesc& desc);
  [[nodiscard]] bool is_headless() const override;
  [[nodiscard]] DeviceApiKind api_kind() const override;
  void SetVSync(bool enabled) override;
  [[nodiscard]] bool vsync() const override;
  UINT CurrentBbIndex() const;
  ~D3D12Device() override;
  [[nodiscard]] std::uint32_t width() const override;
  [[nodiscard]] std::uint32_t height() const override;
  Status BeginFrame() override;
  Status Clear(const ColorRgba& color) override;
  Status DrawSimpleMesh() override;
  Status DispatchCompute(const ComputeDispatchDesc& desc) override;
  Status SetupInstanceCullCompute(const std::filesystem::path& cs_dxil) override;
  Status DispatchInstanceCull(const Mat4& view_proj, std::uint32_t instance_count,
                              std::uint32_t& out_visible) override;
  Status SetupLightTileCullCompute(const std::filesystem::path& cs_dxil) override;
  Status DispatchLightTileCull(const Mat4& view_proj, std::span<const Vec3> positions,
                               std::span<const float> ranges, std::array<int, 128>& out_counts,
                               std::array<int, 1024>& out_indices, const Vec3& eye,
                               const Vec3& cam_forward) override;
  bool TryDispatchLightTileCullGpu(const Mat4& view_proj, std::span<const Vec3> positions,
                                   std::span<const float> ranges, std::array<int, 128>& out_counts,
                                   std::array<int, 1024>& out_indices, const Vec3& eye,
                                   const Vec3& cam_forward);
  Status ProbeBindlessMinimalPath(std::uint32_t srv_heap_slot) override;
  float BindlessAlbedoHeapPad(float tex_slot) const;
  Status SetSubmitConfig(const SubmitConfig& cfg) override;
  Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override;
  Status ReadbackDepthRgbaStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override;
  Status Present() override;
  Status Resize(std::uint32_t width, std::uint32_t height) override;
  Status SetupSimpleMesh(const SimpleMeshShaders& shaders) override;
  Status SetupLitMesh(const LitMeshShaders& shaders) override;
  Status SetFrameLighting(const FrameLighting& lighting) override;
  Status BeginShadowPass() override;
  Status BindShadowCascade(int cascade_index) override;
  Status DrawShadowCubes(std::span<const LitDrawItem> items) override;
  void SetDrawViewport(float x, float y, float w, float h) override;
  void SetPreferLdrTarget(bool on) override;
  void BindSceneColorTargets();
  Status EndShadowPass() override;
  Status BeginLocalShadowPass() override;
  Status BindLocalShadowTile(int tile_index) override;
  Status EndLocalShadowPass() override;
  Status DrawLitCube(const LitDrawItem& item) override;
  Status DrawLitCubes(std::span<const LitDrawItem> items) override;
  Status DrawTransparentLitCubes(std::span<const LitDrawItem> items) override;
  void GpuPassBegin(const char* name) override;
  void GpuPassEnd() override;
  [[nodiscard]] bool GpuTimestampAvailable() const override;
  [[nodiscard]] std::vector<GpuPassTiming> LastGpuPassTimings() const override;
  Status DrawLitCubesWithPso(std::span<const LitDrawItem> items, ID3D12PipelineState* pso);
  Status UploadInstanceTransforms(std::span<const Mat4> worlds) override;
  Status DrawLitInstanced(const LitDrawItem& prototype, std::uint32_t instance_count) override;
  ID3D12Resource* CurrentInstanceBuf() const;
  static Mat4 MakeProbeFaceVp(const Vec3& probe_pos, int face);
  Status EnsureProbeFaceTargets(int face_size);
  Status CaptureReflectionProbeGpu(const Vec3& probe_pos, int face_size,
                                   std::span<const LitDrawItem> items) override;
  Status UploadIndirectIndexedArgs(std::span<const std::uint32_t> raw_u32) override;
  Status ExecuteIndirectIndexed(std::uint32_t draw_count) override;
  Status TryMeshShaderHotPath() override;
  Status SetupPostMesh(const PostShaders& shaders) override;
  Status ResolvePostEffects(const PostResolveDesc& desc) override;
  Status DrawScreenQuads(std::span<const ScreenQuad> quads) override;
  Status DrawDebugLines(std::span<const DebugLineVertex> lines_as_segments) override;
  Status SetupUiMesh(const SimpleMeshShaders& shaders) override;
  Status UploadUiFontAtlas(const std::uint8_t* rgba, int width, int height) override;
  Status DrawUiMesh(std::span<const UiVertex> vertices, std::span<const std::uint16_t> indices,
                    std::span<const UiDrawCmd> commands) override;
 private:
  static constexpr UINT kMaxLitDraws = 64;
  static constexpr UINT kShadowVpSlots = 16;  // 4 cascades + local tiles
  static constexpr UINT64 kFrameCbBytes = 16384;  // ≥ FrameData (32 lights + 8×4×4 clusters)
  static constexpr UINT64 kPostCbBytes = 512;

  UINT64 FrameCbOffset() const;

  UINT64 PostCbOffset() const;

  UINT64 SkyCbOffset() const;

  UINT64 ObjectCbOffset(std::size_t draw_index) const;

  UINT64 ShadowVpCbOffset(int slot) const;

  static constexpr int kMaxMeshSlots = 16;
  static constexpr UINT kShadowMapSize = 2048;
  static constexpr UINT kLocalShadowMapSize = 2048;

  struct MeshSlotGpu {
    ComPtr<ID3D12Resource> vb;
    ComPtr<ID3D12Resource> ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT index_count = 0;
  };

  Status CreateShadowMap();


  Status CreateLocalShadowMap();


  Status UploadRgbaTexture(ComPtr<ID3D12Resource>& tex, UINT srv_slot, const std::uint8_t* rgba,
                           int width, int height);


  Status CreateLitAlbedoTexture();


  Status CreateLitOrmTexture();


  Status CreateLitAlbedoTextureSlot1();


  Status CreateLitOrmTextureSlot1();


  Status BindReflectionCubeSrv();


  Status EnsureDefaultReflectionCubemap();


  Status UploadReflectionCubemap(const std::uint8_t* rgba_faces, int face_size) override;


  Status BindCubeSrv(UINT slot, ID3D12Resource* cube);


  Status UploadIblIrradianceCubemap(const std::uint8_t* rgba_faces, int face_size) override;


  Status UploadIblPrefilterCubemap(const std::uint8_t* rgba_faces, int face_size) override;


  Status UploadIblBrdfLut(const std::uint8_t* rgba, int w, int h) override;


  Status UploadProbeIrradianceAtlas(const float* rgb, int count, int nx, int ny, int nz) override;


  Status UploadSoftShadowMask(const float* factors, int width, int height) override;


  Status EnsureDefaultProbeGiAndSoftShadowTextures();


  Status SetupSkybox(const std::filesystem::path& vs_dxil,
                     const std::filesystem::path& ps_dxil) override;


  Status UploadSkyCubemap(const std::uint8_t* rgba_faces, int face_size) override;


  Status DrawSkybox(const Mat4& view_rot_proj) override;


  Status UploadCubemapResource(ComPtr<ID3D12Resource>& cube, const std::uint8_t* rgba_faces,
                               int face_size);


  Status UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height, int slot) override;


  Status UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height, int slot) override;


  Status SetupScreenQuads(const std::filesystem::path& vs_path,
                          const std::filesystem::path& ps_path);


  Status SetupDebugLines(const std::filesystem::path& vs_path,
                         const std::filesystem::path& ps_path);


  Status CreateCubeMesh();


  Status UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                           std::span<const std::uint32_t> indices) override;


  Status CreateLitConstantBuffers();


  Status CreateSwapchain();


  Status CreateOffscreenBackbuffers();


  void TryEnableDisplayHdr();


  Status CreateFrameResources();


  Status CreateRenderTargets();


  Status CreateDepthBuffer();


  Status CreatePostColorTargets();


  void UpdatePostSrvs();


  Status CreateVertexBuffer();


  Status CreateCheckerTexture();


  void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                  D3D12_RESOURCE_STATES after);


  // Wait for previously *submitted* GPU work. Does not mark in-flight frame
  // allocators idle — stamping fence_values_ mid-frame caused DEVICE_REMOVED
  // when ScreenQuad/debug buffers grew (e.g. after wheel zoom / particles).
  void WaitGpuSubmitted();


  // Full idle: safe to Reset all frame allocators afterward.
  void WaitGpu();


  Status EnsureColorReadbackBuffer();


  Status EnsureDepthReadbackBuffer();


  Status CreateGpuTimestampResources();


  void ReadbackGpuPassTimings(std::uint32_t frame);


  HWND hwnd_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  float draw_vp_x_ = 0.f;
  float draw_vp_y_ = 0.f;
  float draw_vp_w_ = 0.f;
  float draw_vp_h_ = 0.f;
  bool draw_vp_on_ = false;
  bool prefer_ldr_ = false;
  bool gpu_headless_ = false;
  int adapter_index_ = -1;
  bool vsync_ = false;
  bool allow_tearing_ = false;
  bool enable_hdr_output_ = false;
  bool bindless_capable_ = false;
  SIZE_T bindless_probe_gpu_ptr_ = 0;
  bool hdr_output_active_ = false;
  UINT offscreen_bb_index_ = 0;
  SubmitConfig submit_cfg_{};
  std::array<ComPtr<ID3D12CommandAllocator>, 4> worker_allocators_{};
  bool mesh_ready_ = false;
  bool lit_ready_ = false;
  bool quad_ready_ = false;
  bool debug_ready_ = false;
  bool ui_ready_ = false;
  bool ui_font_uploaded_ = false;
  bool post_ready_ = false;
  bool post_resolved_this_frame_ = false;
  bool shadow_active_ = false;
  bool local_shadow_active_ = false;
  int bound_shadow_slot_ = 0;
  std::uint32_t compute_dispatches_ = 0;
  bool cull_ready_ = false;
  bool tile_cull_ready_ = false;
  bool tile_cull_gpu_ = false;
  ComPtr<ID3D12RootSignature> cull_root_;
  ComPtr<ID3D12PipelineState> cull_pso_;
  ComPtr<ID3D12RootSignature> tile_cull_root_;
  ComPtr<ID3D12PipelineState> tile_cull_pso_;
  ComPtr<ID3D12Resource> cull_compact_buf_;
  UINT64 cull_compact_bytes_ = 0;
  D3D12_RESOURCE_STATES cull_compact_state_ = D3D12_RESOURCE_STATE_COMMON;
  std::uint32_t lit_draws_ = 0;
  std::uint32_t shadow_draws_ = 0;
  std::uint32_t screen_quad_draws_ = 0;
  FrameLighting lighting_{};
  D3D12_RESOURCE_STATES shadow_map_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES local_shadow_map_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES depth_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES scene_color_state_ = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES history_state_ = D3D12_RESOURCE_STATE_COMMON;
  UINT quad_vb_capacity_ = 0;
  UINT ui_vb_capacity_ = 0;
  UINT ui_ib_capacity_ = 0;

  ComPtr<IDXGIFactory6> factory_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<IDXGISwapChain3> swapchain_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  ComPtr<ID3D12DescriptorHeap> srv_heap_;
  ComPtr<ID3D12DescriptorHeap> shadow_dsv_heap_;
  ComPtr<ID3D12DescriptorHeap> local_shadow_dsv_heap_;
  ComPtr<ID3D12DescriptorHeap> shadow_srv_heap_;
  ComPtr<ID3D12DescriptorHeap> post_srv_heap_;
  ComPtr<ID3D12DescriptorHeap> hdr_rtv_heap_;
  UINT rtv_descriptor_size_ = 0;
  UINT cbv_srv_uav_descriptor_size_ = 0;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> backbuffers_{};
  // DXGI flip swapchain buffers start in COMMON; offscreen RTs are created as PRESENT.
  std::array<D3D12_RESOURCE_STATES, kFrameCount> backbuffer_states_{};
  std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> allocators_{};
  ComPtr<ID3D12GraphicsCommandList> command_list_;
  ComPtr<ID3D12Resource> dsv_;
  ComPtr<ID3D12Resource> shadow_map_;
  ComPtr<ID3D12Resource> local_shadow_map_;
  ComPtr<ID3D12Resource> lit_albedo_;
  ComPtr<ID3D12Resource> lit_orm_;
  ComPtr<ID3D12Resource> lit_albedo2_;
  ComPtr<ID3D12Resource> lit_orm2_;
  ComPtr<ID3D12Resource> reflection_cube_;   // t10 Fresnel / local probe
  ComPtr<ID3D12Resource> probe_gi_atlas_;    // t11 DDGI-lite irradiance atlas
  ComPtr<ID3D12Resource> soft_shadow_mask_;  // t12 half-res soft-shadow factor
  ComPtr<ID3D12Resource> ibl_prefilter_;     // t6 IBL specular prefilter
  ComPtr<ID3D12Resource> ibl_irradiance_;    // t7
  ComPtr<ID3D12RootSignature> sky_root_;
  ComPtr<ID3D12PipelineState> sky_pso_;
  ComPtr<ID3D12DescriptorHeap> sky_srv_heap_;
  ComPtr<ID3D12Resource> sky_cb_;
  ComPtr<ID3D12Resource> sky_cube_;
  bool sky_ready_ = false;
  bool sky_uploaded_ = false;
  ComPtr<ID3D12Resource> ibl_brdf_lut_;
  std::array<MeshSlotGpu, kMaxMeshSlots> mesh_slots_{};
  ComPtr<ID3D12Resource> scene_color_;
  ComPtr<ID3D12Resource> history_;
  ComPtr<ID3D12Resource> color_readback_;
  std::uint32_t color_readback_w_ = 0;
  std::uint32_t color_readback_h_ = 0;
  ComPtr<ID3D12Resource> depth_readback_;
  std::uint32_t depth_readback_w_ = 0;
  std::uint32_t depth_readback_h_ = 0;
  ColorRgba last_clear_{0.14f, 0.16f, 0.20f, 1.f};
  ComPtr<ID3D12Resource> vertex_buffer_;
  ComPtr<ID3D12Resource> texture_;
  ComPtr<ID3D12Resource> texture_upload_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pso_;
  D3D12_VERTEX_BUFFER_VIEW vbv_{};
  ComPtr<ID3D12RootSignature> lit_root_;
  ComPtr<ID3D12PipelineState> lit_pso_;
  ComPtr<ID3D12PipelineState> lit_pso_transparent_;
  ComPtr<ID3D12RootSignature> mesh_shader_root_;
  ComPtr<ID3D12PipelineState> mesh_shader_pso_;
  ComPtr<ID3D12RootSignature> shadow_root_;
  ComPtr<ID3D12PipelineState> shadow_pso_;
  ComPtr<ID3D12RootSignature> quad_root_;
  ComPtr<ID3D12PipelineState> quad_pso_;
  ComPtr<ID3D12RootSignature> debug_root_;
  ComPtr<ID3D12PipelineState> debug_pso_;
  ComPtr<ID3D12Resource> debug_cb_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> debug_vb_{};
  UINT debug_vb_capacity_ = 0;
  ComPtr<ID3D12RootSignature> post_root_;
  ComPtr<ID3D12PipelineState> post_pso_;
  ComPtr<ID3D12Resource> frame_cb_;
  ComPtr<ID3D12Resource> shadow_frame_cb_;
  ComPtr<ID3D12Resource> object_cb_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> instance_bufs_{};
  std::array<UINT64, kFrameCount> instance_buf_bytes_{};
  ComPtr<ID3D12Resource> indirect_args_buf_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> indirect_args_upload_{};
  ComPtr<ID3D12Resource> indirect_zero_upload_;
  UINT64 indirect_args_bytes_ = 0;
  std::array<UINT64, kFrameCount> indirect_args_upload_bytes_{};
  D3D12_RESOURCE_STATES indirect_args_state_ = D3D12_RESOURCE_STATE_COMMON;
  ComPtr<ID3D12CommandSignature> draw_indexed_cmd_sig_;
  ComPtr<ID3D12Resource> probe_face_color_;
  ComPtr<ID3D12Resource> probe_face_depth_;
  ComPtr<ID3D12DescriptorHeap> probe_rtv_heap_;
  ComPtr<ID3D12DescriptorHeap> probe_dsv_heap_;
  int probe_face_size_ = 0;
  int probe_cube_size_ = 0;
  D3D12_RESOURCE_STATES reflection_cube_state_ = D3D12_RESOURCE_STATE_COMMON;
  ComPtr<ID3D12Resource> post_cb_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> quad_vb_{};
  ComPtr<ID3D12RootSignature> ui_root_;
  ComPtr<ID3D12PipelineState> ui_pso_;
  ComPtr<ID3D12Resource> ui_cb_;
  ComPtr<ID3D12Resource> ui_font_;
  ComPtr<ID3D12Resource> ui_font_upload_;
  ComPtr<ID3D12DescriptorHeap> ui_srv_heap_;
  std::array<ComPtr<ID3D12Resource>, kFrameCount> ui_vb_{};
  std::array<ComPtr<ID3D12Resource>, kFrameCount> ui_ib_{};
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  UINT64 fence_value_ = 0;
  std::array<UINT64, kFrameCount> fence_values_{};
  std::uint32_t frame_index_ = 0;

  ComPtr<ID3D12QueryHeap> timestamp_heap_;
  ComPtr<ID3D12Resource> timestamp_readback_;
  UINT64 timestamp_freq_ = 0;
  std::string gpu_pass_names_[kMaxGpuPasses];
  UINT gpu_pass_count_ = 0;
  UINT timestamp_cursor_ = 0;
  std::array<UINT, kFrameCount> frame_gpu_pass_counts_{};
  std::array<std::array<std::string, kMaxGpuPasses>, kFrameCount> frame_gpu_pass_names_{};
  std::array<bool, kFrameCount> frame_timestamps_pending_{};
  std::vector<GpuPassTiming> last_gpu_timings_;
};

}  // namespace engine::rhi

