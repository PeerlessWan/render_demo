#pragma once

#include "engine/rhi/i_device.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/render/local_lights.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#if defined(ENGINE_HAS_X11)
#include <X11/Xlib.h>
#endif
#if defined(ENGINE_HAS_WAYLAND)
#include <wayland-client.h>
#endif
#include "engine/platform/linux/window_x11.h"
#include "engine/platform/linux/window_wayland.h"
#endif
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace engine::rhi {


namespace device_detail {

constexpr std::uint32_t kFramesInFlight = 2;
// Match D3D12: 4 CSM cascades + local-shadow tiles share one UB ring.
constexpr std::uint32_t kShadowVpSlots = 16;
// Opaque scene + CPU-expanded scale instances (Sandbox uses up to 1024).
constexpr std::uint32_t kMaxLitDraws = 2048;
constexpr VkDeviceSize kUniformAlign = 256;
constexpr std::uint32_t kShadowMapSize = 2048;
constexpr std::uint32_t kLocalShadowMapSize = 2048;
constexpr int kMaxMeshSlots = 16;
constexpr std::uint32_t kMaxScreenQuads = 1024;
constexpr std::uint32_t kMaxDebugVerts = 16384;
constexpr std::uint32_t kMaxUiVerts = 16384;
constexpr std::uint32_t kMaxUiIndices = 49152;
static constexpr VkFormat kHdrColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

inline std::string VkErr(VkResult r) {
    return "VkResult=" + std::to_string(static_cast<int>(r));
}

inline Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return Result<std::vector<std::uint8_t>>::Fail("Cannot open shader: " + path.string());
    }
    const auto size = static_cast<std::size_t>(f.tellg());
    if (size == 0) {
        return Result<std::vector<std::uint8_t>>::Fail("Empty shader: " + path.string());
    }
    std::vector<std::uint8_t> data(size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return Result<std::vector<std::uint8_t>>::Ok(std::move(data));
}

struct FrameGpu {
    float view_proj[16];
    float cascade_vp[4][16];
    float sun_dir[3];
    float sun_intensity;
    float ambient[3];
    float shadow_bias;
    float sun_color[3];
    float specular_power;
    float eye[3];
    float enable_shadow;
    float cascade_splits[4];
    float cam_forward[3];
    float cascade_count;
    float tiles_per_row;
    float enable_ibl;
    float ibl_intensity;
    float enable_reflection;
    float reflection_intensity;
    float local_count;
    float enable_taa;
    float _pad_before_lights[2];
    float local_pos_range[32][4];
    float local_color_intensity[32][4];
    float local_spot[32][4];     // xyz=dir, w=cosOuter (-1 = point/omni)
    float local_spot_inner[32];  // cosInner for lights 0..31
    float local_shadow_vp[12][16];
    float enable_local_shadow;
    float local_shadow_bias;
    float local_shadow_count;
    float local_shadow_tiles;
    float prev_view_proj[16];
    float jitter_x;
    float jitter_y;
    float _pad_jitter[2];
    float local_ies[32];  // C03/W7
    // Mega-W10 C02: packed Forward+ tile×Z lists (must match lit_cube_vk.hlsl).
    float enable_tiled_lights;
    float tile_grid_w;
    float tile_grid_h;
    float max_lights_per_tile;
    float z_slices;
    float z_near;
    float z_far;
    float _pad_z;
    float tile_light_count[128];
    float tile_light_index[1024];
    // W20 L0: match lit_cube_vk.hlsl
    float enable_probe_gi;
    float probe_gi_intensity;
    float probe_rgb_scale;
    float probe_nx;
    float probe_origin[3];
    float probe_ny;
    float probe_spacing[3];
    float probe_nz;
    float enable_soft_shadow_mask;
    float _pad_w20[3];
};

struct ShadowFrameGpu {
    float view_proj[16];
};

struct ObjectGpu {
    float world[16];
    float color[4];
    float metallic;
    float roughness;
    float use_albedo;
    float use_orm;
    float tex_slot;
    float uv_scale;
    float use_instances;
    float pad;
};

struct MeshSlotGpu {
    VkBuffer vb = VK_NULL_HANDLE;
    VkBuffer ib = VK_NULL_HANDLE;
    VkDeviceMemory vb_mem = VK_NULL_HANDLE;
    VkDeviceMemory ib_mem = VK_NULL_HANDLE;
    std::uint32_t index_count = 0;
    VkIndexType index_type = VK_INDEX_TYPE_UINT32;
};

struct Tex2DGpu {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

constexpr VkDeviceSize kFrameUbSize =
        (sizeof(FrameGpu) + kUniformAlign - 1) / kUniformAlign * kUniformAlign;

// Must match post_ssao_taa_vk.hlsl / D3D12 ResolvePostEffects packing.
struct PostCB {
    float inv_res[2];
    float enable_ssao;
    float enable_taa;
    float ssao_radius;
    float ssao_intensity;
    float taa_blend;
    float exposure;
    float inv_view_proj[16];
    float view_proj[16];
    float eye[3];
    float tonemap_mode;
    float enable_auto_exposure;
    float auto_exposure_key;
    float enable_bloom;
    float bloom_threshold;
    float bloom_intensity;
    float enable_fog;
    float fog_density;
    float fog_start;
    float fog_color[3];
    float enable_tonemap;
    float enable_ssr;
    float ssr_intensity;
    float ssr_thickness;
    float enable_dof;
    float dof_focus;
    float dof_scale;
    float enable_motion_blur;
    float motion_blur_strength;
    float prev_view_proj[16];
    float jitter_x;
    float jitter_y;
    float vignette_strength;
    float film_grain_strength;
    float chromatic_aberration;
    float lens_distortion;
    float light_dirt_strength;
    float flare_strength;
    float pad_c04_a;
    float pad_c04_b;
};
static_assert(sizeof(PostCB) <= 512, "post CB exceeds upload buffer");
constexpr VkDeviceSize kPostUbSize =
        (sizeof(PostCB) + kUniformAlign - 1) / kUniformAlign * kUniformAlign;

// Screen passes: D3D clip Y-up + Vulkan negative viewport height → upright FB.
// Shadow atlas uses the same Y-flip so cascade depth matches D3D UV (proj.y * -0.5).
// Positive-height shadow VP previously inverted V vs D3D → SampleCmp missed casters.
// Neg-height flips winding in FB → lit uses FRONT_FACE_CLOCKWISE with CCW meshes.
inline VkViewport MakeViewport(float x, float y, float w, float h) {
    VkViewport vp{};
    vp.x = x;
    vp.y = y;
    vp.width = w;
    vp.height = h;
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    return vp;
}

inline VkViewport MakeYFlippedViewport(float x, float y, float w, float h) {
    VkViewport vp{};
    vp.x = x;
    vp.y = y + h;
    vp.width = w;
    vp.height = -h;
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    return vp;
}

}  // namespace device_detail

using namespace device_detail;

class VulkanDevice final : public IDevice {
 public:
    Status Init(const DeviceDesc& desc);
    ~VulkanDevice() override;
    [[nodiscard]] DeviceApiKind api_kind() const override;
    [[nodiscard]] std::uint32_t width() const override;
    [[nodiscard]] std::uint32_t height() const override;
    void SetVSync(bool enabled) override;
    [[nodiscard]] bool vsync() const override;
    Status BeginFrame() override;
    Status Clear(const ColorRgba& color) override;
    Status Present() override;
    Status Resize(std::uint32_t width, std::uint32_t height) override;
    Status DrawSimpleMesh() override;
    Status SetupSimpleMesh(const SimpleMeshShaders&) override;
    Status SetupLitMesh(const LitMeshShaders& shaders) override;
    Status SetFrameLighting(const FrameLighting& lighting) override;
    Status BeginShadowPass() override;
    Status BindShadowCascade(int cascade_index) override;
    Status DrawShadowCubes(std::span<const LitDrawItem> items) override;
    Status EndShadowPass() override;
    Status BeginLocalShadowPass() override;
    Status BindLocalShadowTile(int tile) override;
    Status EndLocalShadowPass() override;
    Status UploadInstanceTransforms(std::span<const Mat4> worlds) override;
    Status DrawLitInstanced(const LitDrawItem& prototype, std::uint32_t instance_count) override;
    Status SetupInstanceCullCompute(const std::filesystem::path& cs_spirv) override;
    Status DispatchInstanceCull(const Mat4& view_proj, std::uint32_t instance_count,
                                                            std::uint32_t& out_visible) override;
    Status SetupLightTileCullCompute(const std::filesystem::path& cs_spirv) override;
    Status DispatchLightTileCull(const Mat4& view_proj, std::span<const Vec3> positions,
                                                             std::span<const float> ranges, std::array<int, 128>& out_counts,
                                                             std::array<int, 1024>& out_indices, const Vec3& eye,
                                                             const Vec3& cam_forward) override;
    bool TryDispatchLightTileCullGpu(const Mat4& view_proj, std::span<const Vec3> positions,
                                                                     std::span<const float> ranges, std::array<int, 128>& out_counts,
                                                                     std::array<int, 1024>& out_indices, const Vec3& eye,
                                                                     const Vec3& cam_forward);
    Status UploadIndirectIndexedArgs(std::span<const std::uint32_t> raw_u32) override;
    Status ExecuteIndirectIndexed(std::uint32_t draw_count) override;
    Status ExecuteIndirectIndexedGpu(std::uint32_t draw_count, const LitDrawItem& prototype);
    Status TryMeshShaderHotPath() override;
    Status SetupPostMesh(const PostShaders& shaders) override;
    Status ResolvePostEffects(const PostResolveDesc& desc) override;
    Status UploadReflectionCubemap(const std::uint8_t* rgba_faces, int face_size) override;
    Status UploadIblIrradianceCubemap(const std::uint8_t* rgba_faces, int face_size) override;
    Status UploadIblPrefilterCubemap(const std::uint8_t* rgba_faces, int face_size) override;
    Status UploadIblBrdfLut(const std::uint8_t* rgba, int w, int h) override;
    Status UploadProbeIrradianceAtlas(const float* rgb, int count, int nx, int ny, int nz) override;
    Status UploadSoftShadowMask(const float* factors, int width, int height) override;
    Status ProbeBindlessMinimalPath(std::uint32_t /*srv_heap_slot*/) override;
    float BindlessAlbedoHeapPad(float tex_slot) const;
    Status DrawLitCube(const LitDrawItem& item) override;
    Status DrawTransparentLitCubes(std::span<const LitDrawItem> items) override;
    Status DrawLitCubes(std::span<const LitDrawItem> items) override;
    Status DrawLitCubesWithPipeline(std::span<const LitDrawItem> items, VkPipeline pipeline);
    Status UploadLitAlbedoRgba(const std::uint8_t* rgba, int width, int height,
                                                         int slot) override;
    Status UploadLitOrmRgba(const std::uint8_t* rgba, int width, int height, int slot) override;
    Status UploadLitGeometry(int mesh_slot, std::span<const LitVertex> vertices,
                                                     std::span<const std::uint32_t> indices) override;
    Status DrawScreenQuads(std::span<const ScreenQuad> quads) override;
    Status DrawDebugLines(std::span<const DebugLineVertex> lines_as_segments) override;
    Status SetupUiMesh(const SimpleMeshShaders& shaders) override;
    Status UploadUiFontAtlas(const std::uint8_t* rgba, int width, int height) override;
    Status DrawUiMesh(std::span<const UiVertex> vertices, std::span<const std::uint16_t> indices,
                                        std::span<const UiDrawCmd> commands) override;
    Status SetupSkybox(const std::filesystem::path& vs_dxil,
                                         const std::filesystem::path& ps_dxil) override;
    Status UploadSkyCubemap(const std::uint8_t* rgba_faces, int face_size) override;
    Status DrawSkybox(const Mat4& view_rot_proj) override;
    Status DispatchCompute(const ComputeDispatchDesc& desc) override;
    Status ReadbackTextureStub(std::vector<std::uint8_t>& out_rgba, int& w, int& h) override;
 private:
    void DestroyMeshSlot(MeshSlotGpu& slot);


    void DestroyTex2D(Tex2DGpu& tex);


    void UpdateLitCombinedBinding(std::uint32_t binding, VkImageView view, VkSampler sampler);


    void UpdateLitInstanceBinding(VkBuffer buffer, VkDeviceSize range);


    void UpdateLitCombinedBinding(std::uint32_t binding, VkImageView view, VkSampler sampler,
                                                                VkImageLayout layout);


    Status CreateAndUploadRgba2D(Tex2DGpu& out, const std::uint8_t* rgba, int width, int height);


    Status UploadRgba2D(Tex2DGpu& out, const std::uint8_t* rgba, int width, int height,
                                            std::uint32_t binding, VkSampler sampler);


    Status UploadCubemapTo(VkImage& image, VkDeviceMemory& mem, VkImageView& view,
                                                 const std::uint8_t* rgba_faces, int face_size);


    Status UploadIblCubemapGpu(const std::uint8_t* rgba_faces, int face_size, bool bind_as_irradiance);


    void DestroyIblCube();


    void DestroyPrefilterCube();


    void DestroyReflectionProbeCube();


    void DestroySkyCube();


    void DestroySkyResources();


    void DestroyUiResources();


    void DestroyQuadResources();


    void DestroyDebugResources();


    Status SetupScreenQuads(const std::filesystem::path& vs_path,
                                                    const std::filesystem::path& ps_path);


    Status SetupDebugLines(const std::filesystem::path& vs_path,
                                                 const std::filesystem::path& ps_path);


    Status AcceptIblUploadOnce();


    // load_contents: resume after CaptureSceneColor / mid-frame end without wiping depth.
    // Required so debug grid depth-tests against lit geometry (matches D3D12 DSV reuse).
    Status BeginLitRenderPass(const ColorRgba& color, bool load_contents = false);


    Status BeginPresentRenderPass(const ColorRgba& color, bool load_contents = false);


    Status CreateInstance();


#if defined(__linux__)
    // Mega-W11: VK_KHR_xlib_surface helper (callable from CreateSurface / tests).
    static Status TryCreateXlibSurface(VkInstance instance, void* display, void* window_xid,
                                                                         VkSurfaceKHR* out_surface) {
        if (!instance || !display || !window_xid || !out_surface) {
            return Status::Fail(ErrorCode::InvalidArgument, "TryCreateXlibSurface: null arg");
        }
#if defined(ENGINE_HAS_X11)
        VkXlibSurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        ci.dpy = static_cast<::Display*>(display);
        ci.window = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(window_xid));
        const VkResult r = vkCreateXlibSurfaceKHR(instance, &ci, nullptr, out_surface);
        if (r != VK_SUCCESS) {
            return Status::Fail("vkCreateXlibSurfaceKHR failed: " + VkErr(r));
        }
        return Status::Ok("xlib-surface");
#else
        (void)instance;
        (void)display;
        (void)window_xid;
        (void)out_surface;
        return Status::Fail(ErrorCode::Unavailable,
                                                "TryCreateXlibSurface Unavailable: ENGINE_HAS_X11 off");
#endif
    }
#endif

    Status CreateSurface();


    Status PickPhysicalDevice();


    Status CreateLogicalDevice();


    Status CreateSwapchain();


    Status CreateFrameSync();


    void DestroySwapchainViews();


    void DestroySwapchain();


    Status RecreateSwapchain();


    std::uint32_t FindMemoryType(std::uint32_t type_bits, VkMemoryPropertyFlags props) const {
        for (std::uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
            if ((type_bits & (1u << i)) &&
                    (mem_props_.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    VkDeviceSize ShadowVpUbOffset(int slot) const {
        const int s = (std::max)(0, (std::min)(slot, static_cast<int>(kShadowVpSlots) - 1));
        return (static_cast<VkDeviceSize>(frame_index_) * kShadowVpSlots +
                        static_cast<VkDeviceSize>(s)) *
                     kUniformAlign;
    }

    VkCommandBuffer BeginOneShot() {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = command_pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device_, &ai, &cmd);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        return cmd;
    }

    void EndOneShot(VkCommandBuffer cmd);

    // Wait for work already submitted on graphics_queue_ (fence, not Device/QueueWaitIdle).
    // Use before destroying GPU resources that may still be sampled/drawn by prior frames.
    void WaitGpuSubmitted();


    Status CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                                            VkBuffer& buffer, VkDeviceMemory& memory);


    Status CreateImage(std::uint32_t width, std::uint32_t height, VkFormat format,
                                         VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);


    void BarrierShadowImage(VkCommandBuffer cmd, VkImageLayout old_layout,
                                                    VkImageLayout new_layout);


    void BarrierLocalShadowImage(VkCommandBuffer cmd, VkImageLayout old_layout,
                                                             VkImageLayout new_layout);


    Status ImmediateTransitionShadow(VkImageLayout new_layout);


    Status CreateRenderPass();


    Status CreateLitRenderPass();


    void DestroyPresentRenderPasses();


    Status CreatePresentRenderPass();


    void DestroyDepthOnly();


    Status CreateDepthResources();


    void DestroyFramebuffersOnly();


    Status RecreateHdrFramebuffer();


    Status CreateFramebuffers();


    Status CreateShaderModule(const std::vector<std::uint8_t>& spirv, VkShaderModule& out);


    Status CreateLitPipeline(const std::vector<std::uint8_t>& vs_spv,
                                                     const std::vector<std::uint8_t>& ps_spv);


    Status CreatePostColorRenderPass();


    void DestroyPostFramebuffersOnly();


    Status CreatePostFramebuffers();


    Status EnsurePostUb();


    Status UploadPostCB(const PostResolveDesc& desc);


    void BarrierDepth(VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout);


    void BarrierHistory(VkCommandBuffer cmd, VkImageLayout old_layout, VkImageLayout new_layout);


    Status CopySwapchainToHistory(VkCommandBuffer cmd);


    void DestroyHistoryOnly();


    Status EnsureHistory();


    Status CreatePostPipeline(const std::vector<std::uint8_t>& vs_spv,
                                                        const std::vector<std::uint8_t>& ps_spv);


    Status EnsurePostDescriptors();


    void UpdatePostDescriptors();


    void DestroySceneColorOnly();


    void DestroyPostResources();


    Status EnsureSceneColor();


    Status CaptureSceneColorIntermediate(VkCommandBuffer cmd);


    Status CreateShadowResources(const std::vector<std::uint8_t>& shadow_vs_spv);


    Status CreateCubeMesh();


    Status CreateLitBuffersAndDescriptors();


    void UpdateCullDescriptors();


    void DestroyTileCullCompute();


    void DestroyCullCompute();


    void DestroyIndirectArgsBuffers(bool keep_uploads);


    void DestroyLitResources();


#if defined(_WIN32)
    HWND hwnd_ = nullptr;
#elif defined(__linux__)
    const platform::linux_x11::X11Native* x11_ = nullptr;
    const platform::linux_wayland::WaylandNative* wayland_ = nullptr;
#endif
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    std::uint32_t graphics_family_ = 0;
    std::uint32_t present_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format_{};
    std::vector<VkImage> swapchain_images_;
    VkBuffer color_readback_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory color_readback_mem_ = VK_NULL_HANDLE;
    VkDeviceSize color_readback_size_ = 0;
    std::vector<VkImageView> swapchain_views_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    std::array<VkSemaphore, kFramesInFlight> image_available_{};
    std::array<VkSemaphore, kFramesInFlight> render_finished_{};
    std::array<VkFence, kFramesInFlight> in_flight_fences_{};

    std::uint32_t frame_index_ = 0;
    std::uint32_t image_index_ = 0;
    bool frame_recording_ = false;
    bool cleared_ = false;
    bool pass_active_ = false;
    bool present_pass_active_ = false;
    bool present_pass_load_ = false;
    bool used_graphics_ = false;
    ColorRgba clear_color_{0.f, 0.f, 0.f, 1.f};

    bool lit_ready_ = false;
    bool enable_validation_ = false;
    int adapter_index_ = -1;
    bool vsync_ = false;
    bool vsync_dirty_ = false;
    bool post_stub_ready_ = false;
    bool post_resolve_warned_ = false;
    bool ibl_upload_logged_ = false;
    bool local_shadow_pass_active_ = false;
    float post_exposure_ = 1.f;
    int post_tonemap_mode_ = 2;
    std::vector<std::uint32_t> indirect_args_cpu_;
    std::uint32_t indirect_fallback_instances_ = 0;
    VkBuffer indirect_args_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory indirect_args_mem_ = VK_NULL_HANDLE;
    VkDeviceSize indirect_args_bytes_ = 0;
    std::array<VkBuffer, kFramesInFlight> indirect_args_upload_{};
    std::array<VkDeviceMemory, kFramesInFlight> indirect_args_upload_mem_{};
    std::array<VkDeviceSize, kFramesInFlight> indirect_args_upload_bytes_{};
    VkBuffer indirect_zero_upload_ = VK_NULL_HANDLE;
    VkDeviceMemory indirect_zero_upload_mem_ = VK_NULL_HANDLE;

    bool cull_ready_ = false;
    bool tile_cull_ready_ = false;
    bool tile_cull_gpu_ = false;
    bool descriptor_indexing_available_ = false;
    bool bindless_capable_ = false;
    VkDescriptorSetLayout cull_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout cull_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline cull_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool cull_desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet cull_desc_set_ = VK_NULL_HANDLE;
    VkBuffer cull_compact_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory cull_compact_mem_ = VK_NULL_HANDLE;
    VkDeviceSize cull_compact_bytes_ = 0;
    VkDescriptorSetLayout tile_cull_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout tile_cull_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline tile_cull_pipeline_ = VK_NULL_HANDLE;

    VkPipeline post_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout post_pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout post_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool post_desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet post_desc_set_ = VK_NULL_HANDLE;
    VkSampler post_sampler_ = VK_NULL_HANDLE;
    VkSampler post_point_sampler_ = VK_NULL_HANDLE;
    VkBuffer post_ub_ = VK_NULL_HANDLE;
    VkDeviceMemory post_ub_mem_ = VK_NULL_HANDLE;
    VkRenderPass post_render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> post_framebuffers_;
    VkImage history_image_ = VK_NULL_HANDLE;
    VkDeviceMemory history_mem_ = VK_NULL_HANDLE;
    VkImageView history_view_ = VK_NULL_HANDLE;
    VkImageLayout history_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint32_t history_width_ = 0;
    std::uint32_t history_height_ = 0;
    VkImage scene_color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory scene_color_mem_ = VK_NULL_HANDLE;
    VkImageView scene_color_view_ = VK_NULL_HANDLE;
    VkImageLayout scene_color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint32_t scene_color_width_ = 0;
    std::uint32_t scene_color_height_ = 0;
    bool post_resolved_this_frame_ = false;
    int ibl_lut_w_ = 0;
    int ibl_lut_h_ = 0;
    VkImage ibl_cube_image_ = VK_NULL_HANDLE;
    VkDeviceMemory ibl_cube_mem_ = VK_NULL_HANDLE;
    VkImageView ibl_cube_view_ = VK_NULL_HANDLE;
    VkSampler ibl_sampler_ = VK_NULL_HANDLE;
    VkImage ibl_prefilter_image_ = VK_NULL_HANDLE;
    VkDeviceMemory ibl_prefilter_mem_ = VK_NULL_HANDLE;
    VkImageView ibl_prefilter_view_ = VK_NULL_HANDLE;
    VkImage reflection_probe_image_ = VK_NULL_HANDLE;
    VkDeviceMemory reflection_probe_mem_ = VK_NULL_HANDLE;
    VkImageView reflection_probe_view_ = VK_NULL_HANDLE;
    Tex2DGpu ibl_lut_{};
    Tex2DGpu lit_albedo_[2]{};
    Tex2DGpu lit_orm_[2]{};
    Tex2DGpu probe_gi_atlas_{};    // binding 13 (t11)
    Tex2DGpu soft_shadow_mask_{};  // binding 14 (t12)
    VkSampler lit_linear_sampler_ = VK_NULL_HANDLE;
    bool shadow_pass_active_ = false;
    int bound_cascade_ = -1;
    int bound_shadow_vp_slot_ = 0;
    std::uint32_t lit_draws_this_frame_ = 0;
    std::uint32_t shadow_draws_this_pass_ = 0;
    FrameLighting lighting_{};

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_load_ = VK_NULL_HANDLE;
    VkRenderPass present_render_pass_ = VK_NULL_HANDLE;
    VkRenderPass present_render_pass_load_ = VK_NULL_HANDLE;
    VkFramebuffer hdr_framebuffer_ = VK_NULL_HANDLE;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_mem_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkImageLayout depth_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<VkFramebuffer> framebuffers_;

    VkDescriptorSetLayout lit_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout lit_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline lit_pipeline_ = VK_NULL_HANDLE;
    VkPipeline lit_pipeline_transparent_ = VK_NULL_HANDLE;
    VkDescriptorPool lit_desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet lit_desc_set_ = VK_NULL_HANDLE;
    bool depth_clamp_enabled_ = false;

    VkImage shadow_image_ = VK_NULL_HANDLE;
    VkDeviceMemory shadow_mem_ = VK_NULL_HANDLE;
    VkImageView shadow_view_ = VK_NULL_HANDLE;
    VkSampler shadow_sampler_ = VK_NULL_HANDLE;
    VkRenderPass shadow_render_pass_ = VK_NULL_HANDLE;
    VkRenderPass shadow_render_pass_load_ = VK_NULL_HANDLE;
    VkFramebuffer shadow_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadow_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout shadow_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSet shadow_desc_set_ = VK_NULL_HANDLE;
    VkImageLayout shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage local_shadow_image_ = VK_NULL_HANDLE;
    VkDeviceMemory local_shadow_mem_ = VK_NULL_HANDLE;
    VkImageView local_shadow_view_ = VK_NULL_HANDLE;
    VkFramebuffer local_shadow_framebuffer_ = VK_NULL_HANDLE;
    VkImageLayout local_shadow_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    std::array<MeshSlotGpu, kMaxMeshSlots> mesh_slots_{};
    VkBuffer frame_ub_ = VK_NULL_HANDLE;
    VkDeviceMemory frame_ub_mem_ = VK_NULL_HANDLE;
    VkBuffer shadow_frame_ub_ = VK_NULL_HANDLE;
    VkDeviceMemory shadow_frame_ub_mem_ = VK_NULL_HANDLE;
    VkBuffer object_ub_ = VK_NULL_HANDLE;
    VkDeviceMemory object_ub_mem_ = VK_NULL_HANDLE;
    std::array<VkBuffer, kFramesInFlight> instance_bufs_{};
    std::array<VkDeviceMemory, kFramesInFlight> instance_buf_mems_{};
    std::array<VkDeviceSize, kFramesInFlight> instance_buf_bytes_{};

    // Skybox
    bool sky_ready_ = false;
    bool sky_uploaded_ = false;
    VkPipeline sky_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout sky_pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout sky_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool sky_desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sky_desc_set_ = VK_NULL_HANDLE;
    VkBuffer sky_ub_ = VK_NULL_HANDLE;
    VkDeviceMemory sky_ub_mem_ = VK_NULL_HANDLE;
    VkImage sky_cube_image_ = VK_NULL_HANDLE;
    VkDeviceMemory sky_cube_mem_ = VK_NULL_HANDLE;
    VkImageView sky_cube_view_ = VK_NULL_HANDLE;
    VkSampler sky_cube_sampler_ = VK_NULL_HANDLE;

    // UI
    bool ui_ready_ = false;
    bool ui_font_uploaded_ = false;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout ui_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool ui_desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet ui_desc_set_ = VK_NULL_HANDLE;
    VkSampler ui_sampler_ = VK_NULL_HANDLE;
    VkBuffer ui_ub_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_ub_mem_ = VK_NULL_HANDLE;
    std::array<VkBuffer, kFramesInFlight> ui_vb_{};
    std::array<VkDeviceMemory, kFramesInFlight> ui_vb_mem_{};
    std::array<VkBuffer, kFramesInFlight> ui_ib_{};
    std::array<VkDeviceMemory, kFramesInFlight> ui_ib_mem_{};
    std::uint32_t ui_vb_capacity_ = 0;
    std::uint32_t ui_ib_capacity_ = 0;
    Tex2DGpu ui_font_{};

    // Screen quads
    bool quad_ready_ = false;
    VkPipeline quad_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout quad_pipeline_layout_ = VK_NULL_HANDLE;
    std::array<VkBuffer, kFramesInFlight> quad_vb_{};
    std::array<VkDeviceMemory, kFramesInFlight> quad_vb_mem_{};
    std::uint32_t quad_vb_capacity_ = 0;

    // Debug lines
    bool debug_ready_ = false;
    VkPipeline debug_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout debug_pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout debug_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool debug_desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet debug_desc_set_ = VK_NULL_HANDLE;
    VkBuffer debug_ub_ = VK_NULL_HANDLE;
    VkDeviceMemory debug_ub_mem_ = VK_NULL_HANDLE;
    std::array<VkBuffer, kFramesInFlight> debug_vb_{};
    std::array<VkDeviceMemory, kFramesInFlight> debug_vb_mem_{};
    std::uint32_t debug_vb_capacity_ = 0;
};

}  // namespace engine::rhi

