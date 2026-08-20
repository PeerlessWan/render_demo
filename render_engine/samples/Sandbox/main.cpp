#include "engine/app/application.h"

#include "engine/animation/skeleton.h"
#include "engine/animation/gpu_skin_main.h"
#include "engine/media/upscaler.h"
#include "engine/assets/character_asset.h"
#include "engine/assets/gltf_loader.h"
#include "engine/assets/image_loader.h"
#include "engine/assets/asset_hot_reload.h"
#include "engine/assets/shader_hot_reload.h"
#include "engine/assets/shader_compile_hook.h"
#include "engine/assets/streaming_budget.h"
#include "engine/core/log.h"
#include "engine/debug/console.h"
#include "engine/debug/sandbox_harness.h"
#include "engine/core/feature.h"
#include "engine/gi/lightmap.h"
#include "engine/gi/probe_volume.h"
#include "engine/gi/reflection_probe.h"
#include "engine/gi/scene_capture.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/render/atmosphere.h"
#include "engine/render/ies_profile.h"
#include "engine/render2d/bmfont.h"
#include "engine/render2d/path2d.h"
#include "engine/render2d/world_text.h"
#include "engine/render/ibl_pack.h"
#include "engine/render/instance_draw.h"
#include "engine/render/local_lights.h"
#include "engine/render/occlusion.h"
#include "engine/media/media.h"
#include "engine/mixed/pick.h"
#include "engine/net/net_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/render2d/sprite.h"
#include "engine/terrain/heightmap.h"
#include "engine/terrain/chunk_stream.h"
#include "engine/gameplay/possess_controller.h"
#include "engine/clothing/garment_cloth.h"
#include "engine/ui/immediate_ui.h"
#include "engine/ui/rml_ui.h"
#include "engine/vfx/particles.h"
#include "engine/vfx/gpu_particles.h"
#include "engine/vt/virtual_texture.h"
#include "engine/vfx/trail_ribbon.h"
#include "engine/rhi/backend.h"

#include "sandbox_ui_strings.h"
#include "write_bmp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <psapi.h>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

namespace {

engine::assets::ImageRgba8 ArmToOrm(const engine::assets::ImageRgba8& arm) {
  // Poly Haven ARM is already AO/Rough/Metal — keep channels, force opaque alpha.
  engine::assets::ImageRgba8 orm = arm;
  for (std::size_t i = 3; i < orm.rgba.size(); i += 4) {
    orm.rgba[i] = 255;
  }
  return orm;
}

struct ProcessPerfSnapshot {
  float fps = 0.f;
  float frame_ms = 0.f;
  float cpu_percent = 0.f;
  float working_set_mb = 0.f;
  float private_mb = 0.f;
  float peak_working_set_mb = 0.f;
  std::uint32_t page_fault_count = 0;
};

class ProcessPerfSampler {
 public:
  void Tick(float dt_seconds) {
    // Accumulate every frame; publish UI snapshot at 1 Hz.
    const float dt = (std::max)(dt_seconds, 1e-6f);
    acc_frame_ms_ += dt * 1000.f;
    ++acc_frames_;
    publish_timer_ += dt;
    published_this_tick_ = false;

    FILETIME now_ft{}, create{}, exit_ft{}, kernel{}, user{};
    GetSystemTimeAsFileTime(&now_ft);
    if (GetProcessTimes(GetCurrentProcess(), &create, &exit_ft, &kernel, &user)) {
      ULARGE_INTEGER now{}, k{}, u{};
      now.LowPart = now_ft.dwLowDateTime;
      now.HighPart = now_ft.dwHighDateTime;
      k.LowPart = kernel.dwLowDateTime;
      k.HighPart = kernel.dwHighDateTime;
      u.LowPart = user.dwLowDateTime;
      u.HighPart = user.dwHighDateTime;

      if (have_prev_) {
        const ULONGLONG wall = now.QuadPart - prev_wall_.QuadPart;
        const ULONGLONG cpu =
            (k.QuadPart - prev_kernel_.QuadPart) + (u.QuadPart - prev_user_.QuadPart);
        if (wall > 0) {
          SYSTEM_INFO si{};
          GetSystemInfo(&si);
          const DWORD cores = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1u;
          const float pct = 100.f * static_cast<float>(cpu) /
                            static_cast<float>(wall * static_cast<ULONGLONG>(cores));
          acc_cpu_sum_ += pct;
          ++acc_cpu_n_;
        }
      }
      prev_wall_ = now;
      prev_kernel_ = k;
      prev_user_ = u;
      have_prev_ = true;
    }

    if (publish_timer_ < 1.f) {
      return;
    }

    ProcessPerfSnapshot s;
    if (acc_frames_ > 0) {
      s.frame_ms = acc_frame_ms_ / static_cast<float>(acc_frames_);
      s.fps = 1000.f / (std::max)(s.frame_ms, 0.001f);
    }
    if (acc_cpu_n_ > 0) {
      s.cpu_percent = acc_cpu_sum_ / static_cast<float>(acc_cpu_n_);
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
      s.working_set_mb = static_cast<float>(pmc.WorkingSetSize) / (1024.f * 1024.f);
      s.private_mb = static_cast<float>(pmc.PrivateUsage) / (1024.f * 1024.f);
      s.peak_working_set_mb = static_cast<float>(pmc.PeakWorkingSetSize) / (1024.f * 1024.f);
      s.page_fault_count = pmc.PageFaultCount;
    }
    published_ = s;
    publish_timer_ = 0.f;
    acc_frame_ms_ = 0.f;
    acc_frames_ = 0;
    acc_cpu_sum_ = 0.f;
    acc_cpu_n_ = 0;
    published_this_tick_ = true;
  }

  [[nodiscard]] bool JustPublished() const { return published_this_tick_; }
  [[nodiscard]] ProcessPerfSnapshot Snapshot() const { return published_; }

 private:
  ProcessPerfSnapshot published_{};
  float publish_timer_ = 1.f;  // first UI update ASAP
  float acc_frame_ms_ = 0.f;
  int acc_frames_ = 0;
  float acc_cpu_sum_ = 0.f;
  int acc_cpu_n_ = 0;
  bool have_prev_ = false;
  bool published_this_tick_ = false;
  ULARGE_INTEGER prev_wall_{};
  ULARGE_INTEGER prev_kernel_{};
  ULARGE_INTEGER prev_user_{};
};

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc;
  desc.window.title = "Sandbox — LMB/RMB look | Wheel zoom | MMB pan | WASD | F1/F2";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.14f, 0.16f, 0.20f, 1.f};
  bool use_vulkan = false;
  bool gpu_headless_assert = false;
  bool harness_stdio = false;
  bool list_gpus = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 3;
      }
    } else if (arg == "--gpu-headless") {
      desc.gpu_headless = true;
      desc.window.headless = true;
      gpu_headless_assert = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 3;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      desc.headless_frames = std::atoi(argv[++i]);
    } else if (arg == "--backend=vulkan") {
      desc.backend = engine::rhi::Backend::Vulkan;
      use_vulkan = true;
      desc.window.title = "Sandbox (Vulkan) — LMB/RMB look | Wheel | MMB pan | WASD";
    } else if (arg == "--backend=d3d12") {
      desc.backend = engine::rhi::Backend::D3D12;
      use_vulkan = false;
    } else if (arg == "--list-gpus") {
      list_gpus = true;
    } else if (arg.rfind("--gpu=", 0) == 0) {
      desc.adapter_index = std::atoi(arg.c_str() + 6);
    } else if (arg == "--gpu" && i + 1 < argc) {
      desc.adapter_index = std::atoi(argv[++i]);
    } else if (arg == "--vsync" || arg == "--vsync=1" || arg == "--vsync=on") {
      desc.enable_vsync = true;
    } else if (arg == "--vsync=0" || arg == "--vsync=off" || arg == "--no-vsync") {
      desc.enable_vsync = false;
    } else if (arg == "--harness-stdio" || arg == "--mcp") {
      harness_stdio = true;
      desc.gpu_headless = true;
      desc.window.headless = true;
      gpu_headless_assert = false;
      desc.headless_frames = 100000;
      engine::set_log_info_to_stderr(true);
    } else if (arg.rfind("--frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 9);
    }
  }

  if (list_gpus) {
    const auto backend =
        use_vulkan ? engine::rhi::Backend::Vulkan : engine::rhi::Backend::D3D12;
    std::cout << (use_vulkan ? "Vulkan" : "D3D12") << " adapters:\n";
    const auto adapters = engine::rhi::EnumerateGpuAdapters(backend);
    if (adapters.empty()) {
      std::cout << "  (none)\n";
      return 1;
    }
    for (const auto& a : adapters) {
      std::cout << "  [" << a.index << "] " << a.name
                << (a.discrete ? "  (discrete)" : "")
                << (a.software ? "  (software)" : "") << "  vram≈"
                << (a.dedicated_memory_bytes / (1024ull * 1024ull)) << " MB\n";
    }
    std::cout << "Use: sample_sandbox.exe --backend=" << (use_vulkan ? "vulkan" : "d3d12")
              << " --gpu=N\n";
    return 0;
  }

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  a.set_net(std::make_shared<engine::net::NetSystem>());
  a.set_move_speed(6.5f);
  a.set_look_sensitivity(0.0026f);
  a.set_zoom_sensitivity(0.65f);
  a.set_pan_sensitivity(0.005f);
  a.set_look_with_lmb(true);
  a.set_look_with_rmb(true);
  a.set_hide_cursor_on_look(false);
  a.camera().position = {0.f, 2.2f, 6.2f};
  a.camera().pitch = -0.22f;
  // Slightly larger near plane + dense ground + SV_ClipDistance avoid floating slabs.
  a.camera().z_near = 0.35f;

  auto ground = a.world().CreateNode("ground");
  engine::scene::NodeId terrain_node = engine::scene::kInvalidNode;
  {
    engine::scene::Transform t;
    t.position = {0, 0.f, 0};
    t.scale = {1.f, 1.f, 1.f};
    a.world().set_local_transform(ground, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "ground";
    mesh.never_cull = true;
    constexpr float kHalf = 12.f;
    mesh.local_bounds = {{-kHalf, -0.05f, -kHalf}, {kHalf, 0.05f, kHalf}};
    a.world().set_mesh(ground, mesh);
  }
  for (int i = 0; i < 3; ++i) {
    auto id = a.world().CreateNode("box" + std::to_string(i));
    engine::scene::Transform t;
    t.position = {static_cast<float>(i) * 1.6f - 2.4f, 0.5f, -1.2f};
    a.world().set_local_transform(id, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = (i == 1) ? "metal" : "cube";
    a.world().set_mesh(id, mesh);
  }
  {
    auto glass = a.world().CreateNode("glass");
    engine::scene::Transform t;
    // Keep clear of the camera near plane while orbiting the origin.
    t.position = {2.6f, 1.0f, 2.0f};
    t.scale = {0.85f, 0.85f, 0.85f};
    a.world().set_local_transform(glass, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "glass";
    a.world().set_mesh(glass, mesh);
  }
  {
    auto helmet = a.world().CreateNode("helmet");
    engine::scene::Transform t;
    // Mesh already includes glTF node world xform (see LoadGltfMeshFile).
    // Keep off the suburb block (centered near z=-18).
    t.position = {26.f, 1.05f, 16.f};
    t.scale = {1.f, 1.f, 1.f};
    t.rotation = engine::Quat::FromEulerYxz(0.35f, 0.f, 0.f);
    a.world().set_local_transform(helmet, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "helmet";
    a.world().set_mesh(helmet, mesh);
  }

  engine::render::Environment env;
  env.ambient = {0.20f, 0.21f, 0.24f, 1.f};
  env.clear_color = {0.10f, 0.12f, 0.18f, 1.f};
  env.sun_direction = {0.45f, -1.f, 0.35f};
  env.sun_intensity = 1.85f;
  env.sun_color = {1.f, 0.97f, 0.92f, 1.f};
  env.skybox_enabled = true;
  env.fog_enabled = false;
  env.fog_density = 0.018f;
  env.fog_start = 14.f;
  env.fog_color = {0.55f, 0.62f, 0.78f, 1.f};
  env.exposure = 1.15f;
  // C05 opt-in: tint fog/clear from EvalSkyColor(cam forward). Default off for golden parity.
  env.enable_atmosphere = false;

  engine::render::RenderSystem render;
  engine::render::RenderSystemDesc rdesc;
  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  if (use_vulkan) {
    rdesc.lit_vs = shader_dir / "lit_cube_vk.vs.spv";
    rdesc.lit_ps = shader_dir / "lit_cube_vk.ps.spv";
    rdesc.shadow_vs = shader_dir / "shadow_vk.vs.spv";
    rdesc.post_vs = shader_dir / "post_ssao_taa_vk.vs.spv";
    rdesc.post_ps = shader_dir / "post_ssao_taa_vk.ps.spv";
    rdesc.quad_vs = shader_dir / "quad_vk.vs.spv";
    rdesc.quad_ps = shader_dir / "quad_vk.ps.spv";
    rdesc.debug_vs = shader_dir / "debug_line_vk.vs.spv";
    rdesc.debug_ps = shader_dir / "debug_line_vk.ps.spv";
    rdesc.sky_vs = shader_dir / "skybox_vk.vs.spv";
    rdesc.sky_ps = shader_dir / "skybox_vk.ps.spv";
    rdesc.enable_shadows = true;
    rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  } else {
    rdesc.lit_vs = shader_dir / "lit_cube.vs.cso";
    rdesc.lit_ps = shader_dir / "lit_cube.ps.cso";
    rdesc.shadow_vs = shader_dir / "shadow.vs.cso";
    rdesc.shadow_ps = shader_dir / "shadow.ps.cso";
    rdesc.quad_vs = shader_dir / "quad.vs.cso";
    rdesc.quad_ps = shader_dir / "quad.ps.cso";
    rdesc.post_vs = shader_dir / "post_ssao_taa.vs.cso";
    rdesc.post_ps = shader_dir / "post_ssao_taa.ps.cso";
    rdesc.debug_vs = shader_dir / "debug_line.vs.cso";
    rdesc.debug_ps = shader_dir / "debug_line.ps.cso";
    rdesc.sky_vs = shader_dir / "skybox.vs.cso";
    rdesc.sky_ps = shader_dir / "skybox.ps.cso";
    rdesc.enable_shadows = true;
    rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  }
  if (auto st = render.Init(a.device(), rdesc); !st) {
    engine::LogError(st.message());
    return 1;
  }
  render.ApplyEnvironmentDefaults(env);
  {
    const auto cull_cs =
        use_vulkan ? (shader_dir / "instance_cull_vk.cs.spv") : (shader_dir / "instance_cull_cs.cso");
    if (auto st = a.device().SetupInstanceCullCompute(cull_cs); !st) {
      engine::LogWarn(std::string("Cull CS optional: ") + st.message());
    }
  }
  {
    const auto tile_cs = use_vulkan ? (shader_dir / "light_tile_cull_cs_vk.cs.spv")
                                    : (shader_dir / "light_tile_cull_cs.cso");
    if (auto st = a.device().SetupLightTileCullCompute(tile_cs); !st) {
      engine::LogWarn(std::string("Light tile cull CS optional: ") + st.message());
    }
  }
  if (!use_vulkan) {
    if (auto st = a.device().ProbeBindlessMinimalPath(0); !st) {
      engine::LogWarn(std::string("Bindless Feature path: ") + st.message());
    } else {
      engine::LogInfo("Bindless Feature minimal path OK (bindless_hot_path default OFF; "
                      "SetFeatureOverride to opt in)");
    }
  }
  // Dense ground plane (slot 4). Fine tris reduce near-plane straddling → no floating slab.
  {
    constexpr int kSeg = 128;
    constexpr float kHalf = 12.f;
    std::vector<engine::rhi::LitVertex> gverts;
    std::vector<std::uint32_t> gidx;
    gverts.reserve(static_cast<std::size_t>((kSeg + 1) * (kSeg + 1)));
    gidx.reserve(static_cast<std::size_t>(kSeg * kSeg * 6));
    for (int z = 0; z <= kSeg; ++z) {
      for (int x = 0; x <= kSeg; ++x) {
        const float u = static_cast<float>(x) / static_cast<float>(kSeg);
        const float v = static_cast<float>(z) / static_cast<float>(kSeg);
        engine::rhi::LitVertex vert{};
        vert.px = (u * 2.f - 1.f) * kHalf;
        vert.py = 0.f;
        vert.pz = (v * 2.f - 1.f) * kHalf;
        vert.nx = 0.f;
        vert.ny = 1.f;
        vert.nz = 0.f;
        vert.u = u;
        vert.v = v;
        gverts.push_back(vert);
      }
    }
    for (int z = 0; z < kSeg; ++z) {
      for (int x = 0; x < kSeg; ++x) {
        const std::uint32_t i0 = static_cast<std::uint32_t>(z * (kSeg + 1) + x);
        const std::uint32_t i1 = i0 + 1;
        const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(kSeg + 1);
        const std::uint32_t i3 = i2 + 1;
        // CCW when viewed from +Y (matches FrontCounterClockwise=TRUE).
        gidx.push_back(i0);
        gidx.push_back(i2);
        gidx.push_back(i1);
        gidx.push_back(i1);
        gidx.push_back(i2);
        gidx.push_back(i3);
      }
    }
    if (auto st = a.device().UploadLitGeometry(4, gverts, gidx); !st) {
      engine::LogError(std::string("Ground plane upload failed: ") + st.message());
    } else {
      engine::LogInfo("Ground plane uploaded (slot4, 128x128)");
    }
  }
  std::vector<std::uint8_t> base_albedo_rgba;
  int base_albedo_w = 0;
  int base_albedo_h = 0;
  engine::gi::LightmapImage lightmap_img;
  bool lightmap_ready = false;
  bool enable_lightmap = false;
  bool lightmap_applied = false;
  {
#ifndef ENGINE_CONTENT_DIR_A
#error "ENGINE_CONTENT_DIR_A must be set by CMake"
#endif
    const auto content = std::filesystem::path(ENGINE_CONTENT_DIR_A);
    auto loader = engine::assets::CreateDefaultImageLoader();

    const auto brick_diff = content / "textures" / "ph" / "brick_diff.jpg";
    const auto brick_arm = content / "textures" / "ph" / "brick_arm.jpg";
    bool albedo_ok = false;
    if (auto alb = loader->LoadFile(brick_diff)) {
      base_albedo_rgba = alb->rgba;
      base_albedo_w = alb->width;
      base_albedo_h = alb->height;
      if (auto st = a.device().UploadLitAlbedoRgba(alb->rgba.data(), alb->width, alb->height, 0);
          !st) {
        engine::LogError(st.message());
        return 1;
      } else {
        engine::LogInfo("Albedo slot0: Poly Haven red_brick_03");
        albedo_ok = true;
      }
    } else if (auto alb_fallback = loader->LoadFile(content / "textures" / "albedo_brick.png")) {
      base_albedo_rgba = alb_fallback->rgba;
      base_albedo_w = alb_fallback->width;
      base_albedo_h = alb_fallback->height;
      if (auto st = a.device().UploadLitAlbedoRgba(alb_fallback->rgba.data(), alb_fallback->width,
                                                    alb_fallback->height, 0);
          !st) {
        engine::LogError(st.message());
        return 1;
      } else {
        engine::LogInfo("Albedo slot0: fallback albedo_brick.png");
        albedo_ok = true;
      }
    }
    if (!albedo_ok) {
      engine::LogError("Failed to load any albedo texture");
      return 1;
    }

    {
      const auto lm_path = content / "ibl" / "lightmap.rgba";
      if (auto st = engine::gi::LoadLightmapRgba(lm_path, lightmap_img); st) {
        lightmap_ready = true;
        engine::LogInfo("Lightmap loaded " + std::to_string(lightmap_img.width) + "x" +
                        std::to_string(lightmap_img.height) + " (F1 toggle; coexists with Probe GI)");
      } else {
        engine::LogInfo(std::string("Lightmap unavailable: ") + st.message());
      }
    }

    bool orm_ok = false;
    if (auto arm = loader->LoadFile(brick_arm)) {
      auto orm = ArmToOrm(arm.value());
      if (auto st = a.device().UploadLitOrmRgba(orm.rgba.data(), orm.width, orm.height, 0); !st) {
        engine::LogError(st.message());
        return 1;
      } else {
        engine::LogInfo("ORM slot0: Poly Haven brick ARM");
        orm_ok = true;
      }
    } else if (auto orm_fallback = loader->LoadFile(content / "textures" / "orm_brick.png")) {
      if (auto st = a.device().UploadLitOrmRgba(orm_fallback->rgba.data(), orm_fallback->width,
                                                orm_fallback->height, 0);
          !st) {
        engine::LogError(st.message());
        return 1;
      } else {
        engine::LogInfo("ORM slot0: fallback orm_brick.png");
        orm_ok = true;
      }
    }
    if (!orm_ok) {
      engine::LogError("Failed to load any ORM texture");
      return 1;
    }

    const auto helmet_path = content / "models" / "DamagedHelmet.glb";
    if (auto mesh = engine::assets::LoadGltfMeshFile(helmet_path, *loader)) {
      std::vector<engine::rhi::LitVertex> verts(mesh->vertices.size());
      for (std::size_t i = 0; i < mesh->vertices.size(); ++i) {
        const auto& v = mesh->vertices[i];
        verts[i] = {v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v};
      }
      if (auto st = a.device().UploadLitGeometry(1, verts, mesh->indices); !st) {
        engine::LogError(st.message());
        return 1;
      } else {
        if (mesh->has_albedo) {
          if (auto st = a.device().UploadLitAlbedoRgba(mesh->albedo.rgba.data(), mesh->albedo.width,
                                                       mesh->albedo.height, 1);
              !st) {
            engine::LogError(st.message());
            return 1;
          }
        }
        if (mesh->has_orm) {
          if (auto st = a.device().UploadLitOrmRgba(mesh->orm.rgba.data(), mesh->orm.width,
                                                   mesh->orm.height, 1);
              !st) {
            engine::LogError(st.message());
            return 1;
          }
        }
        engine::LogInfo("DamagedHelmet.glb uploaded (mesh slot1 + tex slot1)");
      }
    } else {
      engine::LogError(std::string("Helmet load failed: ") + mesh.status().message());
      return 1;
    }

    // CC0 Kenney City Kit (Suburban): assemble a real low-poly town block into mesh slot 6.
    {
      const auto models = content / "scenes" / "suburb" / "models";
      const auto place = [](float x, float z, float yaw = 0.f, float s = 1.f) {
        return engine::Mat4::TRS({x, 0.f, z}, engine::Quat::FromEulerYxz(yaw, 0.f, 0.f),
                                 {s, s, s});
      };
      std::vector<engine::assets::GltfMeshInstance> parts;
      const char* buildings[] = {"building-type-a.glb", "building-type-b.glb", "building-type-c.glb",
                                 "building-type-d.glb", "building-type-e.glb", "building-type-h.glb",
                                 "building-type-l.glb", "building-type-t.glb"};
      const float spacing = 14.f;
      for (int i = 0; i < 8; ++i) {
        const int row = i / 4;
        const int col = i % 4;
        parts.push_back({models / buildings[i],
                         place((static_cast<float>(col) - 1.5f) * spacing,
                               (static_cast<float>(row) - 0.5f) * spacing, 0.f, 1.f)});
      }
      parts.push_back({models / "tree-large.glb", place(-22.f, -8.f)});
      parts.push_back({models / "tree-large.glb", place(22.f, 6.f)});
      parts.push_back({models / "tree-small.glb", place(-6.f, 12.f)});
      parts.push_back({models / "tree-small.glb", place(8.f, -14.f)});
      parts.push_back({models / "fence.glb", place(-28.f, 0.f, 1.5708f, 1.2f)});
      parts.push_back({models / "fence.glb", place(28.f, 0.f, 1.5708f, 1.2f)});
      parts.push_back({models / "path-long.glb", place(0.f, 0.f, 0.f, 1.5f)});
      parts.push_back({models / "driveway-short.glb", place(-7.f, -7.f)});
      parts.push_back({models / "planter.glb", place(4.f, 4.f)});
      if (auto scene = engine::assets::AssembleGltfMeshes(parts, *loader)) {
        std::vector<engine::rhi::LitVertex> verts(scene->vertices.size());
        for (std::size_t i = 0; i < scene->vertices.size(); ++i) {
          const auto& v = scene->vertices[i];
          verts[i] = {v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v};
        }
        if (auto st = a.device().UploadLitGeometry(6, verts, scene->indices); !st) {
          engine::LogWarn(std::string("Suburb mesh upload failed: ") + st.message());
        } else {
          // Classic lit path only has albedo slots 0/1 (t1/t4). Prefer suburb colormap on slot1.
          const auto cmap = models / "Textures" / "colormap.png";
          if (auto alb = loader->LoadFile(cmap)) {
            (void)a.device().UploadLitAlbedoRgba(alb->rgba.data(), alb->width, alb->height, 1);
            engine::LogInfo("Suburb colormap uploaded (tex slot1)");
          } else if (scene->has_albedo) {
            (void)a.device().UploadLitAlbedoRgba(scene->albedo.rgba.data(), scene->albedo.width,
                                                  scene->albedo.height, 1);
          }
          auto suburb = a.world().CreateNode("suburb");
          engine::scene::Transform suburb_xf;
          suburb_xf.position = {0.f, 0.f, -18.f};
          a.world().set_local_transform(suburb, suburb_xf);
          engine::scene::MeshRenderer sm;
          sm.mesh_id = "suburb";
          sm.never_cull = true;
          sm.local_bounds = {{-40.f, -1.f, -40.f}, {40.f, 25.f, 40.f}};
          a.world().set_mesh(suburb, sm);
          // Default camera overlooks the town block.
          a.camera().position = {0.f, 22.f, 28.f};
          a.camera().pitch = -0.55f;
          a.camera().yaw = 0.f;
          engine::LogInfo("Suburb city scene ready (Kenney City Kit Suburban, CC0)");
        }
      } else {
        engine::LogWarn(std::string("Suburb assemble skipped: ") + scene.status().message());
      }
    }
  }
  std::vector<engine::render::LocalLight> sandbox_local_lights;
  {
    std::vector<engine::render::LocalLight> lights;
    // Keep local lights small/warm. A wide cool light washed the floor cyan wherever
    // sun/CSM dipped — looked like a floating blue slab that tracked camera motion.
    engine::render::LocalLight lamp;
    lamp.id = 1;
    lamp.position = {1.8f, 2.8f, 1.0f};
    lamp.range = 5.5f;
    lamp.color = {1.f, 0.78f, 0.55f, 1.f};
    lamp.intensity = 1.35f;
    lamp.shadow_resolution = 512;
    lamp.cast_shadows = true;
    lights.push_back(lamp);
    engine::render::LocalLight spot;
    spot.id = 2;
    spot.position = {-2.2f, 3.2f, 0.5f};
    spot.range = 9.f;
    spot.color = {1.f, 0.92f, 0.85f, 1.f};
    spot.intensity = 2.2f;
    spot.direction = {0.35f, -1.f, 0.15f};
    spot.spot_angle_deg = 28.f;
    spot.spot_inner_deg = 12.f;
    spot.ies_profile = 1;  // C03 narrow IES
    spot.cast_shadows = true;
    spot.shadow_resolution = 512;
    lights.push_back(spot);
    sandbox_local_lights = lights;
    render.set_local_lights(lights);
  }

  engine::render::EffectTuning fx = render.effect_tuning();
  fx.sun_intensity = env.sun_intensity;
  fx.ambient_scale = 1.05f;
  fx.exposure = use_vulkan ? 1.f : 1.0f;
  fx.enable_ssao = rdesc.quality.enable_ssao;
  // Default TAA off: Halton lit jitter reads as whole-screen shake when history is weak.
  // Opt-in via Effects panel (still available on both backends).
  fx.enable_taa = false;
  fx.enable_ssr = rdesc.quality.enable_ssr;
  fx.enable_bloom = false;  // Bloom turns the horizon band into a floating white slab on LDR.
  fx.bloom_threshold = 1.1f;
  fx.bloom_intensity = 0.2f;
  fx.enable_auto_exposure = false;
  fx.enable_fog = false;  // env.fog_enabled already false; keep effect flag explicit for parity.
  // gpu-headless: single cascade + disable temporal FX for stable golden/readback.
  if (desc.gpu_headless || harness_stdio) {
    fx.shadow_cascades = 1;
    fx.enable_taa = false;
    fx.enable_ssao = false;
    fx.enable_motion_blur = false;
    auto q = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
    q.enable_ssao = false;
    q.enable_taa = false;
    q.shadow_cascades = 1;
    render.set_quality(q);
  } else {
    fx.shadow_cascades = (std::max)(1, rdesc.quality.shadow_cascades);
  }
  fx.enable_ibl = false;  // enabled after IBL pack load
  fx.enable_reflection_probe = true;
  fx.enable_vt_near_default = true;  // W16 ADR 0040: Sandbox near-default VT demo
  render.set_effect_tuning(fx);

  engine::ui::ImmediateUi imgui;
  bool imgui_ready = false;
  if (!imgui.available()) {
    if (!use_vulkan) {
      engine::LogError("Dear ImGui not available (ENGINE_WITH_IMGUI=0)");
      return 1;
    }
    engine::LogInfo("Dear ImGui not available; continuing without UI panel");
  } else {
    engine::ui::ImmediateUiDesc ui_desc;
    ui_desc.ui_vs = shader_dir / (use_vulkan ? "ui_imgui_vk.vs.spv" : "ui_imgui.vs.cso");
    ui_desc.ui_ps = shader_dir / (use_vulkan ? "ui_imgui_vk.ps.spv" : "ui_imgui.ps.cso");
    if (auto st = imgui.Init(a.device(), ui_desc); !st) {
      engine::LogError(st.message());
      return 1;
    }
    imgui_ready = true;
  }

  auto physics = engine::physics::CreateDefaultPhysicsWorld();
  engine::LogInfo(std::string("Physics backend: ") + physics->backend_name());
  engine::LogInfo(std::string("Retained UI backend: ") +
                  engine::ui::QueryRetainedUiBackend().name);
  bool mouse_left_was = false;
  float pick_press_x = 0.f;
  float pick_press_y = 0.f;
  bool pick_tracking = false;
  engine::physics::RigidBodyDesc falling;
  falling.position = {0, 4, -2};
  const int phys_id = physics->CreateBox(falling);
  auto phys_node = a.world().CreateNode("phys_box");
  {
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(phys_node, mesh);
  }

  // C22 thin SoftBody: soft cube near the falling rigid body (Jolt only; builtin SKIP).
  // Skip under gpu-headless golden/assert so dumps stay pixel-stable.
  int soft_id = -1;
  std::vector<engine::Vec3> soft_verts;
  std::vector<std::uint32_t> soft_indices;
  if (!gpu_headless_assert) {
    engine::physics::SoftBodyDesc soft_desc;
    soft_desc.position = {2.5f, 5.f, -2.f};
    soft_desc.grid = 5;
    soft_desc.cell = 0.18f;
    soft_desc.mass = 2.f;
    soft_id = physics->CreateSoftBody(soft_desc);
    if (soft_id >= 0) {
      (void)physics->SoftBodyGetIndices(soft_id, soft_indices);
      engine::LogInfo("SoftBody created id=" + std::to_string(soft_id) +
                      " indices=" + std::to_string(soft_indices.size()));
    } else {
      engine::LogInfo("SoftBody unsupported on this physics backend (SKIP)");
    }
  }

  // M23: heightfield + water patch + vegetation (density follows QualityTier).
  engine::terrain::Heightmap heightmap;
  heightmap.width = 17;
  heightmap.height = 17;
  heightmap.cell = 0.75f;
  heightmap.samples.resize(static_cast<std::size_t>(17 * 17));
  constexpr float kWaterLevel = 0.05f;
  for (int z = 0; z < 17; ++z) {
    for (int x = 0; x < 17; ++x) {
      const float nx = (x - 8) * 0.22f;
      const float nz = (z - 8) * 0.22f;
      heightmap.samples[static_cast<std::size_t>(z * 17 + x)] =
          0.35f * std::sin(nx * 2.1f) * std::cos(nz * 1.7f);
    }
  }
  // Keep the demo heightmap clear of the ±12 brick ground so vegetation does not
  // pile onto the main plate (reads as a broken/messy D3D12 scene).
  const engine::Vec3 terrain_origin{-40.f, -0.35f, -40.f};
  const auto terrain_mesh = engine::terrain::BuildTerrainMesh(heightmap, terrain_origin);
  if (!terrain_mesh.indices.empty()) {
    std::vector<engine::rhi::LitVertex> tverts(terrain_mesh.positions.size() / 3);
    for (std::size_t i = 0; i < tverts.size(); ++i) {
      tverts[i] = {terrain_mesh.positions[i * 3 + 0], terrain_mesh.positions[i * 3 + 1],
                   terrain_mesh.positions[i * 3 + 2], terrain_mesh.normals[i * 3 + 0],
                   terrain_mesh.normals[i * 3 + 1], terrain_mesh.normals[i * 3 + 2],
                   terrain_mesh.uvs[i * 2 + 0], terrain_mesh.uvs[i * 2 + 1]};
    }
    if (auto st = a.device().UploadLitGeometry(2, tverts, terrain_mesh.indices); st) {
      terrain_node = a.world().CreateNode("terrain");
      engine::scene::Transform tt;
      tt.position = {0, 0, 0};
      a.world().set_local_transform(terrain_node, tt);
      engine::scene::MeshRenderer tm;
      tm.mesh_id = "terrain";
      tm.never_cull = true;
      // Keep terrain patch away from the main ±12 ground plane to avoid dual-floor confusion.
      tm.local_bounds = {{terrain_origin.x, -1.5f, terrain_origin.z},
                         {terrain_origin.x + 16.f * 0.75f, 2.f,
                          terrain_origin.z + 16.f * 0.75f}};
      a.world().set_mesh(terrain_node, tm);
      engine::LogInfo("Terrain heightmap mesh uploaded (slot2)");
    }
  }
  {
    const auto water_mesh = engine::terrain::BuildWaterPatchMesh(6.f);
    if (!water_mesh.indices.empty()) {
      std::vector<engine::rhi::LitVertex> wverts(water_mesh.positions.size() / 3);
      for (std::size_t i = 0; i < wverts.size(); ++i) {
        wverts[i] = {water_mesh.positions[i * 3 + 0], water_mesh.positions[i * 3 + 1],
                     water_mesh.positions[i * 3 + 2], water_mesh.normals[i * 3 + 0],
                     water_mesh.normals[i * 3 + 1], water_mesh.normals[i * 3 + 2],
                     water_mesh.uvs[i * 2 + 0], water_mesh.uvs[i * 2 + 1]};
      }
      if (auto st = a.device().UploadLitGeometry(5, wverts, water_mesh.indices); st) {
        auto water_node = a.world().CreateNode("water");
        engine::scene::Transform wt;
        // Center of heightmap patch, slightly above water_level sample band.
        wt.position = {terrain_origin.x + 8.f * heightmap.cell,
                       terrain_origin.y + kWaterLevel, terrain_origin.z + 8.f * heightmap.cell};
        a.world().set_local_transform(water_node, wt);
        engine::scene::MeshRenderer wm;
        wm.mesh_id = "water";
        wm.never_cull = true;
        wm.local_bounds = {{-6.f, -0.2f, -6.f}, {6.f, 0.2f, 6.f}};
        a.world().set_mesh(water_node, wm);
        engine::LogInfo("Water patch mesh uploaded (slot5)");
      }
    }
  }
  // Scatter at High density; QualityTier caps how many stay visible.
  // Default density is Low so the main plate stays readable; raise via F1 quality.
  const auto veg = engine::terrain::ScatterVegetation(heightmap, kWaterLevel, 2);
  std::vector<engine::scene::NodeId> veg_nodes;
  constexpr std::size_t kVegCapHigh = 48;
  for (std::size_t i = 0; i < veg.size() && i < kVegCapHigh; ++i) {
    auto id = a.world().CreateNode("veg" + std::to_string(i));
    engine::scene::Transform t;
    t.position = {terrain_origin.x + veg[i].position.x,
                  terrain_origin.y + veg[i].position.y + 0.4f,
                  terrain_origin.z + veg[i].position.z};
    t.scale = {0.25f * veg[i].scale, 0.8f * veg[i].scale, 0.25f * veg[i].scale};
    a.world().set_local_transform(id, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(id, mesh);
    veg_nodes.push_back(id);
  }
  auto veg_cap_for_tier = [](engine::render::QualityTier tier) -> std::size_t {
    switch (tier) {
      case engine::render::QualityTier::Low:
        return 8;
      case engine::render::QualityTier::High:
        return kVegCapHigh;
      case engine::render::QualityTier::Medium:
      default:
        return 24;
    }
  };
  std::size_t veg_density_cap = veg_cap_for_tier(engine::render::QualityTier::Low);
  engine::LogInfo("Vegetation instances=" + std::to_string(veg_nodes.size()) +
                  " density_cap=" + std::to_string(veg_density_cap));

  // M6/M14: morph demo mesh (bind cube-ish + smile/frown deltas)
  // M6 skin: glTF JOINTS/WEIGHTS + inverse-bind → animation::SkinVertexCpu
  // (see LoadGltfMeshFile has_skin / unit test "glTF skin joints feed SkinVertexCpu").
  std::vector<engine::Vec3> morph_bind;
  std::vector<engine::animation::MorphTarget> morph_targets(2);
  morph_targets[0].name = "bulge";
  morph_targets[1].name = "squash";
  {
    // Unit cube corners as 8 verts (simplified display mesh via 2 triangles? use 8-corner box)
    const engine::Vec3 corners[8] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
                                     {0.5f, 0.5f, -0.5f},  {-0.5f, 0.5f, -0.5f},
                                     {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},
                                     {0.5f, 0.5f, 0.5f},   {-0.5f, 0.5f, 0.5f}};
    morph_bind.assign(corners, corners + 8);
    morph_targets[0].deltas.resize(8);
    morph_targets[1].deltas.resize(8);
    for (int i = 0; i < 8; ++i) {
      morph_targets[0].deltas[static_cast<std::size_t>(i)] = {0, corners[i].y * 0.35f, 0};
      morph_targets[1].deltas[static_cast<std::size_t>(i)] = {corners[i].x * -0.25f, 0,
                                                              corners[i].z * -0.25f};
    }
  }
  float morph_w0 = 0.35f;
  float morph_w1 = 0.15f;
  float morph_w0_uploaded = -1.f;
  float morph_w1_uploaded = -1.f;
  auto morph_node = a.world().CreateNode("morph");
  {
    engine::scene::Transform t;
    t.position = {-3.2f, 1.0f, 1.2f};
    a.world().set_local_transform(morph_node, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "morph";
    mesh.local_bounds = {{-0.7f, -0.7f, -0.7f}, {0.7f, 0.7f, 0.7f}};
    a.world().set_mesh(morph_node, mesh);
  }

  engine::gi::ProbeVolume probes;
  // Densers grid + incremental update budget (W-gi-deepen). Overlay ambient only;
  // does not replace IBL/Lightmap — F1 "Probe GI" toggles this additive tint.
  probes.Configure({-6.f, 0.25f, -6.f}, {1.5f, 1.25f, 1.5f}, 9, 4, 9);
  probes.set_budget_per_frame(48);
  bool enable_gi = false;

  // M22: lightmap multiply path coexists with Probe GI (independent F1 flags; not DDGI).
  auto sync_lightmap_albedo = [&](bool want) {
    if (base_albedo_rgba.empty()) {
      return;
    }
    if (want == lightmap_applied) {
      return;
    }
    std::vector<std::uint8_t> upload = base_albedo_rgba;
    if (want && lightmap_ready) {
      engine::gi::MultiplyAlbedoByLightmap(upload, base_albedo_w, base_albedo_h, lightmap_img);
    }
    if (auto st = a.device().UploadLitAlbedoRgba(upload.data(), base_albedo_w, base_albedo_h, 0);
        !st) {
      engine::LogError(std::string("Lightmap albedo upload failed: ") + st.message());
      return;
    }
    lightmap_applied = want && lightmap_ready;
  };

  engine::gi::ReflectionProbe reflection_probe;
  reflection_probe.Configure({0.f, 1.6f, 0.f}, 32);
  reflection_probe.UpdateFromEnvironment(env.sun_direction, env.sun_color, 3.f, env.ambient);
  {
    std::vector<engine::gi::SceneCaptureOrb> orbs;
    orbs.push_back({{1.5f, 1.2f, 0.f}, {1.f, 0.4f, 0.2f, 1.f}, 0.8f});
    orbs.push_back({{-1.2f, 1.5f, 1.f}, {0.3f, 0.6f, 1.f, 1.f}, 0.6f});
    std::vector<std::uint8_t> faces;
    engine::gi::CaptureApproximateSceneFaces(faces, reflection_probe.face_size(),
                                             reflection_probe.position(), orbs, env.sun_direction,
                                             env.sun_color, env.sun_intensity, env.ambient);
    (void)a.device().UploadReflectionCubemap(faces.data(), reflection_probe.face_size());
  }
  reflection_probe.ClearDirty();

  constexpr int kScaleInstances = 1024;
  std::vector<engine::Mat4> scale_worlds(kScaleInstances);
  for (int i = 0; i < kScaleInstances; ++i) {
    const int x = i % 32;
    const int z = i / 32;
    // Keep the stress grid off the ±12 brick plate so the default view stays readable.
    scale_worlds[static_cast<std::size_t>(i)] = engine::Mat4::TRS(
        {static_cast<float>(x) * 0.85f - 13.f, 0.35f, static_cast<float>(z) * 0.85f - 40.f}, {},
        {0.25f, 0.7f, 0.25f});
  }
  bool show_scale_instances = false;
  engine::render::OcclusionBuffer scale_occ;
  scale_occ.Configure(64, 64);
  // Soft HiZ: far-plane default (no occluders) so frustum cull still runs.
  {
    std::vector<float> depth(64 * 64, 1.f);
    scale_occ.UploadDepthFinest(depth);
  }

  // Optional IBL pack next to content or shaders.
  {
    namespace fs = std::filesystem;
    const fs::path candidates[] = {
        fs::path(ENGINE_CONTENT_DIR_A) / "ibl" / "ibl_pack.ibl1",
        fs::path(ENGINE_SHADER_DIR_A) / "ibl_pack.ibl1",
        fs::path("content") / "ibl" / "ibl_pack.ibl1",
    };
    for (const auto& p : candidates) {
      if (!fs::exists(p)) {
        continue;
      }
      auto pack = engine::render::LoadIblPack(p);
      if (!pack) {
        engine::LogError(pack.status().message());
        break;
      }
      (void)a.device().UploadIblIrradianceCubemap(pack.value().irradiance_rgba.data(),
                                                 pack.value().face_size);
      (void)a.device().UploadIblPrefilterCubemap(pack.value().prefilter_rgba.data(),
                                                pack.value().face_size);
      (void)a.device().UploadIblBrdfLut(pack.value().brdf_lut_rgba.data(), pack.value().lut_w,
                                       pack.value().lut_h);
      env.ibl_irradiance = p.string();
      env.ibl_prefilter = p.string();
      env.ibl_brdf_lut = p.string();
      fx.enable_ibl = true;
      render.set_effect_tuning(fx);
      engine::LogInfo("IBL pack loaded: " + p.string());
      break;
    }
  }

  // Skybox cubemap (CC0 Poly Haven Kloppenheim 06 Pure Sky via ibl_baker).
  {
    namespace fs = std::filesystem;
    const fs::path sky_candidates[] = {
        fs::path(ENGINE_CONTENT_DIR_A) / "ibl" / "sky_kloppenheim06.sky1",
        fs::path("content") / "ibl" / "sky_kloppenheim06.sky1",
    };
    for (const auto& p : sky_candidates) {
      if (!fs::exists(p)) {
        continue;
      }
      auto sky = engine::render::LoadSkyCubemap(p);
      if (!sky) {
        engine::LogWarn(sky.status().message());
        break;
      }
      if (auto st = a.device().UploadSkyCubemap(sky.value().rgba_faces.data(), sky.value().face_size);
          !st) {
        engine::LogWarn(std::string("UploadSkyCubemap: ") + st.message());
        break;
      }
      env.skybox_cubemap = p.string();
      fx = render.effect_tuning();
      fx.enable_skybox = true;
      render.set_effect_tuning(fx);
      engine::LogInfo("Skybox loaded: " + p.string());
      break;
    }
  }

  engine::vfx::ParticleEmitter particles;
  particles.Configure({1.8f, 2.6f, 1.0f}, 28.f, 1.1f);
  engine::vfx::GpuParticleSystem gpu_particles;
  gpu_particles.Configure({1.8f, 2.6f, 1.0f}, 28.f, 1.1f, 128);
  engine::SetFeatureOverride("gpu_particles", true);
  // W16 ADR 0040: VT near-default demo (not full-material default).
  engine::SetFeatureOverride("virtual_texture", true);
  engine::SetFeatureOverride("vt_near_default", true);
  engine::vt::VirtualTexture sandbox_vt;
  sandbox_vt.Configure(16, 32, 3);
  engine::vfx::TrailRibbon lamp_trail;
  lamp_trail.Configure(0.75f, 0.06f, 40);

  engine::scene::NodeId picked_node = engine::scene::kInvalidNode;

  bool panel_open = true;
  // Mega-W10: default free fly; F toggles possess walk/jump.
  // Mega-W11: prefer content/characters/*.glb, else capsule mesh.
  // Character spawn: map center (suburb block origin at z=-18).
  engine::gameplay::PossessController possess;
  possess.possess_character = false;
  possess.position = {0.f, 0.f, -18.f};
  possess.facing_yaw = 0.f;
  possess.SetSampleHeight([](float /*x*/, float /*z*/) { return 0.f; });
  bool possess_third_person = false;
  bool possess_was = false;
  engine::assets::CharacterLoadResult possess_character_mesh;
  {
    auto char_images = engine::assets::CreateDefaultImageLoader();
    const auto characters_dir =
        std::filesystem::path(ENGINE_CONTENT_DIR_A) / "characters";
    possess_character_mesh = engine::assets::CharacterAsset::TryLoadFromCharactersDirOrCapsule(
        characters_dir, *char_images, possess.params.capsule_radius,
        possess.params.capsule_height);
    engine::LogInfo(std::string("Possess CharacterAsset: ") + possess_character_mesh.note +
                    " verts=" + std::to_string(possess_character_mesh.mesh.vertices.size()) +
                    " fallback=" +
                    (possess_character_mesh.used_capsule_fallback ? "true" : "false"));
  }
  // W12/W17: lit character mesh (slot 7+) for possess; draw_parts → slots 8..
  engine::SetFeatureOverride("gpu_skinning", true);
  engine::scene::NodeId character_node = engine::scene::kInvalidNode;
  bool character_mesh_uploaded = false;
  std::vector<int> character_mesh_slots;
  std::vector<engine::scene::NodeId> character_part_nodes;
  {
    const auto& cm = possess_character_mesh.mesh;
    if (!cm.vertices.empty() && !cm.indices.empty()) {
      auto upload_one = [&](int slot, const engine::assets::GltfMeshAsset& mesh) -> bool {
        std::vector<engine::rhi::LitVertex> verts(mesh.vertices.size());
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
          const auto& v = mesh.vertices[i];
          verts[i] = {v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.u, v.v};
        }
        return static_cast<bool>(a.device().UploadLitGeometry(slot, verts, mesh.indices));
      };
      if (upload_one(7, cm)) {
        character_mesh_slots.push_back(7);
        character_mesh_uploaded = true;
        int slot = 8;
        for (const auto& part : possess_character_mesh.draw_parts) {
          if (slot >= 16) {
            break;
          }
          if (part.vertices.empty() || part.indices.empty()) {
            continue;
          }
          if (upload_one(slot, part)) {
            character_mesh_slots.push_back(slot);
            ++slot;
          }
        }
        character_node = a.world().CreateNode("character");
        engine::scene::Transform xf;
        xf.position = possess.position;
        a.world().set_local_transform(character_node, xf);
        engine::scene::MeshRenderer mr;
        mr.mesh_id = "character";
        mr.never_cull = true;
        mr.local_bounds = {{-0.8f, 0.f, -0.8f}, {0.8f, 2.2f, 0.8f}};
        a.world().set_mesh(character_node, mr);
        a.world().set_visible(character_node, false);
        for (std::size_t i = 1; i < character_mesh_slots.size(); ++i) {
          const int s = character_mesh_slots[i];
          auto nid = a.world().CreateNode("character_part");
          a.world().set_local_transform(nid, xf);
          engine::scene::MeshRenderer pmr;
          pmr.mesh_id = "character_" + std::to_string(s);
          pmr.never_cull = true;
          pmr.local_bounds = mr.local_bounds;
          a.world().set_mesh(nid, pmr);
          a.world().set_visible(nid, false);
          character_part_nodes.push_back(nid);
        }
        engine::LogInfo("W17: character lit mesh uploaded slots=" +
                        std::to_string(character_mesh_slots.size()));
      } else {
        engine::LogWarn("character UploadLitGeometry failed");
      }
    }
  }
  auto sandbox_upscaler = engine::media::CreateUpscaler();
  engine::LogInfo(std::string("W12 upscaler: ") + sandbox_upscaler->name());
  engine::clothing::GarmentCloth cape;
  {
    engine::clothing::GarmentMeshDesc cape_desc;
    cape_desc.kind = engine::clothing::GarmentKind::Cape;
    cape_desc.rows = 5;
    cape_desc.cols = 4;
    cape.Generate(cape_desc, {0.f, 1.55f, 0.f});
  }
  bool large_terrain_mode = false;
  engine::terrain::Heightmap large_hm;
  bool large_hm_ok = false;
  engine::terrain::TerrainChunkStreamer terrain_chunks;
  engine::assets::StreamingBudget terrain_budget(32ull * 1024ull * 1024ull);
  terrain_chunks.Configure(32.f, 2, 64ull * 1024ull);
  bool f_was_down = false;
  bool profiler_open = true;
  bool enable_vsync = desc.enable_vsync;
  int ui_lang_i = static_cast<int>(SandboxUiLang::Zh);
  // Prefer OS UI language when English; otherwise default Chinese for Win target users.
  if (PRIMARYLANGID(LANGIDFROMLCID(GetUserDefaultLCID())) == LANG_ENGLISH) {
    ui_lang_i = static_cast<int>(SandboxUiLang::En);
  }
  bool show_grid = false;
  bool show_axes = false;
  bool show_physics_debug = false;
  bool show_world_text_debug = false;
  bool show_path2d_debug = false;
  bool f1_was_down = false;
  bool f2_was_down = false;
  bool f3_was_down = false;
  bool f4_was_down = false;
  bool f5_was_down = false;
  bool record_png = false;
  int record_frame_index = 0;
  std::filesystem::path record_dir;
  std::chrono::steady_clock::time_point record_last_sample{};
  std::string record_last_path;
  std::string record_last_error;
  std::uint64_t record_dropped = 0;
  std::unique_ptr<sandbox::AsyncBmpWriter> record_writer;
  engine::debug::Profiler profiler;
  render.SetProfiler(&profiler);
  ProcessPerfSampler process_perf;
  ProcessPerfSnapshot displayed_perf{};
  std::vector<std::pair<std::string, double>> displayed_cpu_scopes;
  std::vector<engine::rhi::GpuPassTiming> displayed_gpu_passes;

  std::vector<engine::render2d::Sprite> sprites;

  auto audio = engine::media::CreateDefaultAudioDevice();
  engine::LogInfo(std::string("Audio backend: ") + audio->backend_name());
  engine::LogInfo(
      "Sandbox: free cam LMB/RMB look | F Character (FPS mouse look, Esc unlock) | TP toggle in UI");

  // C16 / Mega-W9: hot-reload poll → rebuild lit PSO / reload albedo (not log-only).
  // Mega-W10: optional `engine::assets::TryCompileHlslWithDxc` when dxc.exe is on PATH
  // (else Unavailable SKIP); offline tools/shader_compile remains the primary path.
  engine::assets::ShaderHotReload shader_hot;
  shader_hot.SetShaderDir(shader_dir);
  engine::assets::AssetHotReload asset_hot;
  asset_hot.SetRoot(std::filesystem::path(ENGINE_CONTENT_DIR_A));
  (void)shader_hot.Poll();
  (void)asset_hot.Poll();

  auto RebuildLitPsoIfPossible = [&]() -> bool {
    if (auto st = render.Init(a.device(), rdesc); !st) {
      engine::LogWarn(std::string("ShaderHotReload: RebuildLitPsoIfPossible failed: ") +
                      st.message());
      return false;
    }
    // Re-bind optional CS after lit PSO rebuild (Init only sets lit/post/sky).
    {
      const auto cull_cs = use_vulkan ? (shader_dir / "instance_cull_vk.cs.spv")
                                      : (shader_dir / "instance_cull_cs.cso");
      (void)a.device().SetupInstanceCullCompute(cull_cs);
    }
    {
      const auto tile_cs = use_vulkan ? (shader_dir / "light_tile_cull_cs_vk.cs.spv")
                                      : (shader_dir / "light_tile_cull_cs.cso");
      (void)a.device().SetupLightTileCullCompute(tile_cs);
    }
    engine::LogInfo("ShaderHotReload: RebuildLitPsoIfPossible Ok");
    return true;
  };

  auto ReloadTextureIfPossible = [&]() -> bool {
    const auto content = std::filesystem::path(ENGINE_CONTENT_DIR_A);
    auto loader = engine::assets::CreateDefaultImageLoader();
    const auto brick_diff = content / "textures" / "ph" / "brick_diff.jpg";
    const auto fallback = content / "textures" / "albedo_brick.png";
    if (auto alb = loader->LoadFile(brick_diff)) {
      base_albedo_rgba = alb->rgba;
      base_albedo_w = alb->width;
      base_albedo_h = alb->height;
      if (auto st = a.device().UploadLitAlbedoRgba(alb->rgba.data(), alb->width, alb->height, 0);
          !st) {
        engine::LogWarn(std::string("AssetHotReload: ReloadTexture failed: ") + st.message());
        return false;
      }
      lightmap_applied = false;
      engine::LogInfo("AssetHotReload: ReloadTexture Ok (brick_diff)");
      return true;
    }
    if (auto alb = loader->LoadFile(fallback)) {
      base_albedo_rgba = alb->rgba;
      base_albedo_w = alb->width;
      base_albedo_h = alb->height;
      if (auto st = a.device().UploadLitAlbedoRgba(alb->rgba.data(), alb->width, alb->height, 0);
          !st) {
        engine::LogWarn(std::string("AssetHotReload: ReloadTexture failed: ") + st.message());
        return false;
      }
      lightmap_applied = false;
      engine::LogInfo("AssetHotReload: ReloadTexture Ok (albedo_brick fallback)");
      return true;
    }
    // Clear CPU cache so next lightmap sync does not stale-upload.
    base_albedo_rgba.clear();
    base_albedo_w = 0;
    base_albedo_h = 0;
    lightmap_applied = false;
    engine::LogWarn("AssetHotReload: ReloadTexture — source missing; cleared albedo cache");
    return false;
  };

  bool headless_assert_failed = false;
  const auto status = a.Run([&](engine::Application& app_ref) {
    profiler.Begin("Frame");
    if (shader_hot.Poll() && shader_hot.ConsumePsoRebuildRequest()) {
      // W17: optional dxc online compile when on PATH (else SKIP, still rebuild PSO from .cso).
      const auto lit_hlsl = std::filesystem::path(ENGINE_CONTENT_DIR_A).parent_path() /
                            "shaders" / "hlsl" / "lit_cube.hlsl";
      // Prefer repo shaders/hlsl via ENGINE_SHADER sources next to binary.
      const auto lit_from_shader_dir = shader_dir.parent_path().parent_path() / "shaders" / "hlsl" /
                                       "lit_cube.hlsl";
      const auto hlsl_try =
          std::filesystem::exists(lit_from_shader_dir) ? lit_from_shader_dir : lit_hlsl;
      if (std::filesystem::exists(hlsl_try)) {
        const auto cst = engine::assets::TryCompileHlslWithDxc(hlsl_try);
        if (!cst) {
          engine::LogInfo(std::string("ShaderHotReload dxc: ") + cst.message());
        }
      }
      (void)RebuildLitPsoIfPossible();
    }
    if (asset_hot.Poll() && asset_hot.ConsumeInvalidateRequest()) {
      (void)ReloadTextureIfPossible();
    }
    // Q1 golden/deterministic: freeze physics + particles under gpu-headless assert.
    if (!gpu_headless_assert) {
      physics->Step(app_ref.delta_time());
      {
        engine::scene::Transform t = app_ref.world().local_transform(phys_node);
        t.position = physics->body_position(phys_id);
        app_ref.world().set_local_transform(phys_node, t);
      }
    }
    app_ref.world().UpdateTransforms();

    const auto& snap = app_ref.window().input_snapshot();
    const bool f1_down = snap.keys[VK_F1];
    if (f1_down && !f1_was_down) {
      panel_open = !panel_open;
    }
    f1_was_down = f1_down;
    const bool f2_down = snap.keys[VK_F2];
    if (f2_down && !f2_was_down) {
      profiler_open = !profiler_open;
    }
    f2_was_down = f2_down;
    const bool f3_down = snap.keys[VK_F3];
    if (f3_down && !f3_was_down) {
      show_grid = !show_grid;
    }
    f3_was_down = f3_down;
    const bool f4_down = snap.keys[VK_F4];
    if (f4_down && !f4_was_down) {
      show_axes = !show_axes;
    }
    f4_was_down = f4_down;

    const bool f_down = snap.keys['F'] || snap.keys['f'];
    if (f_down && !f_was_down) {
      possess.possess_character = !possess.possess_character;
    }
    f_was_down = f_down;

    auto apply_possess_mode = [&](bool on) {
      app_ref.set_fly_locomotion_enabled(!on);
      app_ref.set_always_look(on);
      app_ref.set_look_pitch_limit(1.2f);  // ~69° look up/down clamp
      if (on) {
        app_ref.RelockCursor();
        // Enter: default first-person; sync body + camera yaw.
        possess_third_person = false;
        possess.facing_yaw = app_ref.camera().yaw;
        app_ref.camera().pitch = 0.f;
        app_ref.camera().position = possess.FirstPersonCameraPosition();
        engine::LogInfo(
            "Character ON — FPS (WASD/Space, mouse look, Esc unlock UI; enable Third-person in panel)");
      } else {
        app_ref.set_cursor_unlocked(false);
        app_ref.set_always_look(false);
        app_ref.set_look_pitch_limit(1.5f);
        engine::LogInfo("Character OFF — free fly camera (LMB/RMB look)");
      }
    };

    if (possess.possess_character != possess_was) {
      apply_possess_mode(possess.possess_character);
      possess_was = possess.possess_character;
    }

    if (possess.possess_character) {
      const bool rmb = snap.mouse_right;
      // Mainstream TP: WASD vs camera; mouse orbits camera; RMB syncs body facing to camera yaw.
      if (possess_third_person && rmb && !app_ref.cursor_unlocked() && !app_ref.ui_want_capture()) {
        possess.facing_yaw = app_ref.camera().yaw;
      }
      if (!possess_third_person) {
        // First-person: body follows look yaw.
        possess.facing_yaw = app_ref.camera().yaw;
      }

      engine::gameplay::PossessInput pin;
      pin.move_x = app_ref.input().axis("MoveX");
      pin.move_z = app_ref.input().axis("MoveZ");
      pin.jump = app_ref.input().pressed("Jump") || app_ref.input().key_down(engine::input::Key::Space);
      pin.move_yaw = app_ref.camera().yaw;
      possess.Step(app_ref.delta_time(), pin);

      if (possess_third_person) {
        app_ref.camera().position =
            possess.ThirdPersonCameraPosition(app_ref.camera().yaw, app_ref.camera().pitch);
      } else {
        app_ref.camera().position = possess.FirstPersonCameraPosition();
      }

      // W12: drive lit character node (visible in TP or when cursor unlocked).
      if (character_mesh_uploaded && character_node != engine::scene::kInvalidNode) {
        engine::scene::Transform xf;
        xf.position = possess.position;
        xf.rotation = engine::Quat::FromEulerYxz(possess.facing_yaw, 0.f, 0.f);
        app_ref.world().set_local_transform(character_node, xf);
        const bool show_body = possess_third_person || app_ref.cursor_unlocked();
        app_ref.world().set_visible(character_node, show_body);
        for (auto nid : character_part_nodes) {
          app_ref.world().set_local_transform(nid, xf);
          app_ref.world().set_visible(nid, show_body);
        }
      }

      std::vector<engine::Vec3> attach;
      const engine::Quat fq_attach = engine::Quat::FromEulerYxz(possess.facing_yaw, 0.f, 0.f);
      attach.push_back(possess.position + fq_attach.Rotate({-0.2f, 1.45f, 0.f}));
      attach.push_back(possess.position + fq_attach.Rotate({0.2f, 1.45f, 0.f}));
      cape.SetAttachPoints(attach);
      engine::clothing::CapsuleCollider col;
      col.center = possess.CapsuleCenter();
      col.radius = possess.params.capsule_radius * 1.05f;
      col.half_height = possess.params.capsule_height * 0.35f;
      cape.Step(app_ref.delta_time(), &col);
      for (std::size_t i = 1; i < cape.positions.size(); ++i) {
        app_ref.debug_draw().AddLine(cape.positions[i - 1], cape.positions[i],
                                     {0.85f, 0.55f, 0.2f, 1.f});
      }
      // Third-person (or unlocked cursor): draw character mesh rotated by facing_yaw.
      if (possess_third_person || app_ref.cursor_unlocked()) {
        const auto& cm = possess_character_mesh.mesh;
        const engine::Vec3 origin = possess.position;
        const std::size_t tri_budget =
            (std::min)(cm.indices.size() / 3, static_cast<std::size_t>(48));
        const engine::Quat fq = engine::Quat::FromEulerYxz(possess.facing_yaw, 0.f, 0.f);
        for (std::size_t t = 0; t < tri_budget; ++t) {
          const auto i0 = cm.indices[t * 3 + 0];
          const auto i1 = cm.indices[t * 3 + 1];
          const auto i2 = cm.indices[t * 3 + 2];
          if (i0 >= cm.vertices.size() || i1 >= cm.vertices.size() ||
              i2 >= cm.vertices.size()) {
            continue;
          }
          auto xf = [&](std::size_t ii) {
            const engine::Vec3 lp{cm.vertices[ii].px, cm.vertices[ii].py, cm.vertices[ii].pz};
            return fq.Rotate(lp) + origin;
          };
          const engine::Vec3 a = xf(i0);
          const engine::Vec3 b = xf(i1);
          const engine::Vec3 c = xf(i2);
          const engine::ColorRgba tint = possess_character_mesh.used_capsule_fallback
                                            ? engine::ColorRgba{0.45f, 0.75f, 0.95f, 1.f}
                                            : engine::ColorRgba{0.55f, 0.9f, 0.55f, 1.f};
          app_ref.debug_draw().AddLine(a, b, tint);
          app_ref.debug_draw().AddLine(b, c, tint);
          app_ref.debug_draw().AddLine(c, a, tint);
        }
      }
    } else {
      app_ref.set_fly_locomotion_enabled(true);
      if (character_mesh_uploaded && character_node != engine::scene::kInvalidNode) {
        app_ref.world().set_visible(character_node, true);  // show idle character in free cam
        engine::scene::Transform xf;
        xf.position = possess.position;
        xf.rotation = engine::Quat::FromEulerYxz(possess.facing_yaw, 0.f, 0.f);
        app_ref.world().set_local_transform(character_node, xf);
        for (auto nid : character_part_nodes) {
          app_ref.world().set_local_transform(nid, xf);
          app_ref.world().set_visible(nid, true);
        }
      }
    }

    auto start_or_stop_record = [&](bool enable) {
      if (enable == record_png) {
        return;
      }
      record_png = enable;
      if (record_png) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm{};
#if defined(_WIN32)
        localtime_s(&local_tm, &tt);
#else
        local_tm = *std::localtime(&tt);
#endif
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d_%02d%02d%02d", local_tm.tm_year + 1900,
                      local_tm.tm_mon + 1, local_tm.tm_mday, local_tm.tm_hour, local_tm.tm_min,
                      local_tm.tm_sec);
        record_dir = std::filesystem::path("captures") / (std::string("sandbox_") + stamp);
        std::error_code ec;
        std::filesystem::create_directories(record_dir, ec);
        record_frame_index = 0;
        record_last_sample = {};
        record_last_path.clear();
        record_last_error.clear();
        record_dropped = 0;
        if (ec) {
          record_last_error = ec.message();
          engine::LogError("Record BMP: create_directories failed: " + record_last_error);
          record_png = false;
        } else {
          record_writer = std::make_unique<sandbox::AsyncBmpWriter>();
          engine::LogInfo("Record BMP ON → " + record_dir.string() +
                          " (~60 Hz, async write; may drop if disk lags)");
        }
      } else {
        if (record_writer) {
          record_dropped = record_writer->dropped();
          record_writer.reset();  // drains queue
        }
        engine::LogInfo("Record BMP OFF (" + std::to_string(record_frame_index) + " queued, " +
                        std::to_string(record_dropped) + " dropped)");
      }
    };

    const bool f5_down = snap.keys[VK_F5];
    if (f5_down && !f5_was_down) {
      start_or_stop_record(!record_png);
    }
    f5_was_down = f5_down;

    auto& dbg = app_ref.debug_draw();
    dbg.Clear();
    if (show_grid) {
      // Slightly above the lit ground; darker lines stay visible on bright brick.
      dbg.AddGrid(8.f, 1.f, 0.05f, {0.22f, 0.24f, 0.28f, 1.f}, {0.45f, 0.48f, 0.55f, 1.f});
    }
    if (show_axes) {
      dbg.AddAxes(2.5f, 0.05f);
    }
    // C14/W7: world BMFont billboard wireframe (atlas glyph boxes facing camera).
    if (show_world_text_debug) {
      engine::render2d::BmFontAtlas atlas;
      atlas.line_height = 16;
      atlas.glyphs['W'] = {0, 0, 10, 14, 11};
      atlas.glyphs['7'] = {10, 0, 8, 14, 9};
      const auto& cam = app_ref.camera();
      const engine::Quat q = engine::Quat::FromEulerYxz(cam.yaw, cam.pitch, 0.f);
      const engine::Vec3 right = q.Rotate(engine::Vec3{1.f, 0.f, 0.f});
      const engine::Vec3 up = q.Rotate(engine::Vec3{0.f, 1.f, 0.f});
      const auto wt = engine::render2d::BuildWorldTextBillboards(
          atlas, "W7", {0.f, 2.4f, -1.5f}, right, up, 0.04f);
      const engine::ColorRgba tc{0.95f, 0.85f, 0.35f, 1.f};
      for (std::size_t qi = 0; qi + 5 < wt.indices.size(); qi += 6) {
        const auto& v0 = wt.vertices[wt.indices[qi + 0]].position;
        const auto& v1 = wt.vertices[wt.indices[qi + 1]].position;
        const auto& v2 = wt.vertices[wt.indices[qi + 2]].position;
        const auto& v3 = wt.vertices[wt.indices[qi + 5]].position;
        dbg.AddLine(v0, v1, tc);
        dbg.AddLine(v1, v2, tc);
        dbg.AddLine(v2, v3, tc);
        dbg.AddLine(v3, v0, tc);
      }
    }
    // W17/G13: Path2D fill (XZ plane) as debug wireframe — visible without sprite upload.
    if (show_path2d_debug) {
      engine::render2d::Path2D path;
      path.MoveTo({-1.5f, -1.0f});
      path.LineTo({1.5f, -1.0f});
      path.QuadraticTo({2.2f, 0.4f}, {0.8f, 1.6f});
      path.LineTo({-0.8f, 1.6f});
      path.LineTo({-1.5f, -1.0f});
      const auto fill = path.EarClipSimple();
      const engine::ColorRgba pc{0.35f, 0.85f, 0.95f, 1.f};
      const float y = 0.08f;
      auto to_world = [y](const engine::render2d::Path2DVertex& v) {
        return engine::Vec3{v.position.x, y, v.position.y};
      };
      if (fill.topology == engine::render2d::Path2DTopology::TriangleList) {
        for (std::size_t i = 0; i + 2 < fill.indices.size(); i += 3) {
          const auto a = to_world(fill.vertices[fill.indices[i]]);
          const auto b = to_world(fill.vertices[fill.indices[i + 1]]);
          const auto c = to_world(fill.vertices[fill.indices[i + 2]]);
          dbg.AddLine(a, b, pc);
          dbg.AddLine(b, c, pc);
          dbg.AddLine(c, a, pc);
        }
      }
      const auto stroke = path.BuildLineList();
      const engine::ColorRgba sc{0.95f, 0.55f, 0.2f, 1.f};
      for (std::size_t i = 0; i + 1 < stroke.indices.size(); i += 2) {
        dbg.AddLine(to_world(stroke.vertices[stroke.indices[i]]),
                    to_world(stroke.vertices[stroke.indices[i + 1]]), sc);
      }
    }
    if (show_physics_debug) {
      for (int bi = 0; bi < physics->body_count(); ++bi) {
        const auto p = physics->body_position(bi);
        const auto he = physics->body_half_extents(bi);
        engine::Aabb box;
        box.min = {p.x - he.x, p.y - he.y, p.z - he.z};
        box.max = {p.x + he.x, p.y + he.y, p.z + he.z};
        dbg.AddAabb(box, {0.2f, 0.95f, 0.35f, 1.f});
      }
      // SoftBody wireframe: read vertices from physics, draw face edges (interactive only).
      if (!gpu_headless_assert && soft_id >= 0 &&
          physics->SoftBodyGetVertices(soft_id, soft_verts)) {
        const engine::ColorRgba soft_color{0.95f, 0.45f, 0.2f, 1.f};
        if (!soft_indices.empty()) {
          for (std::size_t i = 0; i + 2 < soft_indices.size(); i += 3) {
            const auto i0 = soft_indices[i];
            const auto i1 = soft_indices[i + 1];
            const auto i2 = soft_indices[i + 2];
            if (i0 >= soft_verts.size() || i1 >= soft_verts.size() || i2 >= soft_verts.size()) {
              continue;
            }
            dbg.AddLine(soft_verts[i0], soft_verts[i1], soft_color);
            dbg.AddLine(soft_verts[i1], soft_verts[i2], soft_color);
            dbg.AddLine(soft_verts[i2], soft_verts[i0], soft_color);
          }
        } else {
          for (std::size_t i = 0; i + 1 < soft_verts.size(); ++i) {
            dbg.AddLine(soft_verts[i], soft_verts[i + 1], soft_color);
          }
        }
      }
    }
    if (picked_node != engine::scene::kInvalidNode && app_ref.world().valid(picked_node)) {
      const auto pos = app_ref.world().world_matrix(picked_node).TransformPoint({0, 0, 0});
      engine::Aabb box;
      box.min = {pos.x - 0.6f, pos.y - 0.6f, pos.z - 0.6f};
      box.max = {pos.x + 0.6f, pos.y + 0.6f, pos.z + 0.6f};
      dbg.AddAabb(box, {1.f, 0.85f, 0.15f, 1.f});
    } else {
      picked_node = engine::scene::kInvalidNode;
    }

    const float dw = static_cast<float>(app_ref.window().width());
    const float dh = static_cast<float>(app_ref.window().height());
    if (imgui_ready) {
      imgui.BeginFrame(snap, dw, dh, app_ref.delta_time());
      process_perf.Tick(app_ref.delta_time());
      if (process_perf.JustPublished()) {
        displayed_perf = process_perf.Snapshot();
        displayed_cpu_scopes.clear();
        displayed_cpu_scopes.reserve(profiler.samples_ms().size());
        for (const auto& [name, ms] : profiler.samples_ms()) {
          displayed_cpu_scopes.emplace_back(name, ms);
        }
        displayed_gpu_passes = app_ref.device().LastGpuPassTimings();
      }
      const auto& perf = displayed_perf;
      const auto& S = SandboxUi(static_cast<SandboxUiLang>(ui_lang_i));

      // Always-on performance HUD (top-right).
      {
        const float pw = 260.f;
        const float ph = 148.f;
        if (imgui.BeginWindow(S.perf, dw - pw - 16.f, 16.f, pw, ph)) {
          char line[160];
          std::snprintf(line, sizeof(line), "%s  %.1f", S.fps, perf.fps);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "%s  %.2f ms", S.frame_ms, perf.frame_ms);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "%s  %.1f %%", S.cpu_pct, perf.cpu_percent);
          imgui.Text(line);
          imgui.Separator();
          std::snprintf(line, sizeof(line), "%s  %.1f MB", S.working_set, perf.working_set_mb);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "%s  %.1f MB", S.private_mem, perf.private_mb);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "%s  %.1f MB", S.peak_ws, perf.peak_working_set_mb);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "%s  %u", S.page_faults, perf.page_fault_count);
          imgui.Text(line);
        }
        imgui.EndWindow();
      }

      if (panel_open) {
        if (imgui.BeginWindow(S.effects, 16.f, 48.f, 360.f, 780.f)) {
          const char* lang_items[] = {S.lang_en, S.lang_zh};
          imgui.Combo(S.language, &ui_lang_i, lang_items, 2);
          const auto& Su = SandboxUi(static_cast<SandboxUiLang>(ui_lang_i));
          imgui.Text(Su.help_look);
          imgui.Text(Su.help_move);
          imgui.Separator();
          char perf_line[96];
          std::snprintf(perf_line, sizeof(perf_line), "1s avg: %.0f FPS | %.1f ms | CPU %.0f%%",
                        perf.fps, perf.frame_ms, perf.cpu_percent);
          imgui.Text(perf_line);
          imgui.Separator();
          imgui.Checkbox(Su.show_grid, &show_grid);
          imgui.Checkbox(Su.show_axes, &show_axes);
          imgui.Checkbox("Physics debug (AABB/SoftBody)", &show_physics_debug);
          imgui.Checkbox("World text debug (W7)", &show_world_text_debug);
          imgui.Checkbox("Path2D debug (W17)", &show_path2d_debug);
          imgui.Checkbox("Scale instances (1024)", &show_scale_instances);
          if (imgui.Checkbox("Character (F)", &possess.possess_character)) {
            // Edge applied next frame via possess_was; force sync now.
            apply_possess_mode(possess.possess_character);
            possess_was = possess.possess_character;
          }
          if (possess.possess_character) {
            if (imgui.Checkbox("Third-person camera", &possess_third_person)) {
              if (possess_third_person) {
                const auto& o = possess.params.camera_offset;
                app_ref.camera().pitch =
                    std::atan2(-std::abs(o.y), (std::max)(std::abs(o.z), 1e-3f));
                app_ref.camera().position = possess.ThirdPersonCameraPosition(
                    app_ref.camera().yaw, app_ref.camera().pitch);
                engine::LogInfo("Third-person ON — mouse look camera; hold RMB to turn body");
              } else {
                app_ref.camera().pitch = 0.f;
                app_ref.camera().position = possess.FirstPersonCameraPosition();
                possess.facing_yaw = app_ref.camera().yaw;
                engine::LogInfo("First-person ON — mouse look");
              }
            }
          }
          if (imgui.Checkbox("Large terrain heightmap", &large_terrain_mode)) {
            auto show_large_terrain_view = [&]() {
              possess.SetSampleHeight([&large_hm](float x, float z) {
                return engine::terrain::SampleHeight(large_hm, x, z);
              });
              // Display mesh: subsample so upload stays interactive (full map still for height).
              engine::terrain::Heightmap display = large_hm;
              constexpr int kStep = 4;
              if (large_hm.width > kStep + 1 && large_hm.height > kStep + 1) {
                const int dw = (large_hm.width - 1) / kStep + 1;
                const int dh = (large_hm.height - 1) / kStep + 1;
                display.width = dw;
                display.height = dh;
                display.cell = large_hm.cell * static_cast<float>(kStep);
                display.samples.resize(static_cast<std::size_t>(dw * dh));
                for (int z = 0; z < dh; ++z) {
                  for (int x = 0; x < dw; ++x) {
                    const int sx = (std::min)(x * kStep, large_hm.width - 1);
                    const int sz = (std::min)(z * kStep, large_hm.height - 1);
                    display.samples[static_cast<std::size_t>(z * dw + x)] =
                        large_hm.samples[static_cast<std::size_t>(sz * large_hm.width + sx)];
                  }
                }
              }
              const engine::Vec3 origin{0.f, 0.f, 0.f};
              const auto mesh = engine::terrain::BuildTerrainMesh(display, origin);
              if (mesh.indices.empty()) {
                return false;
              }
              std::vector<engine::rhi::LitVertex> verts(mesh.positions.size() / 3);
              for (std::size_t i = 0; i < verts.size(); ++i) {
                verts[i] = {mesh.positions[i * 3 + 0], mesh.positions[i * 3 + 1],
                            mesh.positions[i * 3 + 2], mesh.normals[i * 3 + 0],
                            mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2],
                            mesh.uvs[i * 2 + 0], mesh.uvs[i * 2 + 1]};
              }
              if (auto st = app_ref.device().UploadLitGeometry(2, verts, mesh.indices); !st) {
                return false;
              }
              if (terrain_node == engine::scene::kInvalidNode ||
                  !app_ref.world().valid(terrain_node)) {
                terrain_node = app_ref.world().CreateNode("terrain");
              }
              engine::scene::Transform tt;
              tt.position = {0, 0, 0};
              app_ref.world().set_local_transform(terrain_node, tt);
              engine::scene::MeshRenderer tm;
              tm.mesh_id = "terrain";
              tm.never_cull = true;
              const float wx = engine::terrain::HeightmapWorldSizeX(display);
              const float wz = engine::terrain::HeightmapWorldSizeZ(display);
              tm.local_bounds = {{0.f, -2.f, 0.f}, {wx, 60.f, wz}};
              app_ref.world().set_mesh(terrain_node, tm);
              app_ref.world().set_visible(terrain_node, true);
              app_ref.world().set_visible(ground, false);
              for (const auto id : veg_nodes) {
                app_ref.world().set_visible(id, false);
              }
              const float mid_x = 0.5f * wx;
              const float mid_z = 0.5f * wz;
              const float mid_y = engine::terrain::SampleHeight(large_hm, mid_x, mid_z) + 55.f;
              app_ref.camera().position = {mid_x, mid_y, mid_z + 120.f};
              app_ref.camera().yaw = 0.f;
              app_ref.camera().pitch = -0.45f;
              engine::LogInfo("Large terrain mesh uploaded " + std::to_string(display.width) + "x" +
                              std::to_string(display.height) + " world≈" + std::to_string(wx) +
                              "m");
              return true;
            };

            if (large_terrain_mode) {
              if (!large_hm_ok) {
                const auto hm_path = std::filesystem::path(ENGINE_CONTENT_DIR_A) /
                                     "scenes/large_terrain/heightmap_512.png";
                auto loaded = engine::terrain::LoadHeightmapPng(hm_path, 2.f, 48.f);
                if (!loaded) {
                  large_terrain_mode = false;
                  engine::LogWarn("Large terrain load failed: " + loaded.status().message());
                } else {
                  large_hm = std::move(loaded.value());
                  large_hm_ok = true;
                  engine::LogInfo("Large terrain loaded: " + hm_path.string());
                  if (!show_large_terrain_view()) {
                    large_terrain_mode = false;
                    engine::LogWarn("Large terrain mesh upload failed");
                  }
                }
              } else if (!show_large_terrain_view()) {
                large_terrain_mode = false;
              }
            } else {
              possess.SetSampleHeight([](float /*x*/, float /*z*/) { return 0.f; });
              app_ref.world().set_visible(ground, true);
              if (terrain_node != engine::scene::kInvalidNode &&
                  app_ref.world().valid(terrain_node)) {
                app_ref.world().set_visible(terrain_node, true);
              }
              app_ref.camera().position = {0.f, 2.2f, 6.2f};
              app_ref.camera().yaw = 0.f;
              app_ref.camera().pitch = -0.22f;
            }
          }
          imgui.Checkbox(Su.probe_gi, &enable_gi);
          {
            char up_line[96];
            std::snprintf(up_line, sizeof(up_line), "Upscaler: %s (ENGINE_UPSCALER)",
                          sandbox_upscaler->name());
            imgui.Text(up_line);
          }
          if (lightmap_ready) {
            if (imgui.Checkbox(Su.lightmap, &enable_lightmap)) {
              sync_lightmap_albedo(enable_lightmap);
            }
          }
          imgui.SliderFloat(Su.morph_bulge, &morph_w0, 0.f, 1.f);
          imgui.SliderFloat(Su.morph_squash, &morph_w1, 0.f, 1.f);
          imgui.Separator();
          imgui.Checkbox(Su.shadows, &fx.enable_shadows);
          imgui.Checkbox(Su.ssao, &fx.enable_ssao);
          imgui.Checkbox(Su.taa, &fx.enable_taa);
          imgui.Checkbox(Su.ibl, &fx.enable_ibl);
          imgui.Checkbox(Su.skybox, &fx.enable_skybox);
          imgui.Checkbox(Su.reflection_probe, &fx.enable_reflection_probe);
          imgui.Checkbox(Su.ssr, &fx.enable_ssr);
          imgui.Checkbox(Su.dof, &fx.enable_dof);
          imgui.Checkbox(Su.motion_blur, &fx.enable_motion_blur);
          imgui.Checkbox(Su.tonemap, &fx.enable_tonemap);
          imgui.Checkbox(Su.auto_exposure, &fx.enable_auto_exposure);
          imgui.Checkbox(Su.bloom, &fx.enable_bloom);
          imgui.Checkbox(Su.fog, &fx.enable_fog);
          imgui.Checkbox(Su.atmosphere, &env.enable_atmosphere);
          imgui.Checkbox(Su.volume_clouds, &env.enable_volume_clouds);
          imgui.SliderFloat(Su.chromatic, &fx.chromatic_aberration, 0.f, 1.f);
          if (imgui.Checkbox(Su.vsync, &enable_vsync)) {
            app_ref.device().SetVSync(enable_vsync);
          }
          {
            bool rec = record_png;
            if (imgui.Checkbox(Su.record_png, &rec)) {
              start_or_stop_record(rec);
            }
            char rec_line[192];
            if (record_png) {
              const std::uint64_t drop =
                  record_writer ? record_writer->dropped() : record_dropped;
              std::snprintf(rec_line, sizeof(rec_line), "%s | #%d | drop %llu | %s", Su.record_on,
                            record_frame_index, static_cast<unsigned long long>(drop),
                            record_dir.string().c_str());
            } else if (!record_last_error.empty()) {
              std::snprintf(rec_line, sizeof(rec_line), "%s | err: %s", Su.record_off,
                            record_last_error.c_str());
            } else if (!record_last_path.empty()) {
              std::snprintf(rec_line, sizeof(rec_line), "%s | last: %s", Su.record_off,
                            record_last_path.c_str());
            } else {
              std::snprintf(rec_line, sizeof(rec_line), "%s", Su.record_off);
            }
            imgui.Text(rec_line);
          }
          imgui.Separator();
          imgui.SliderFloat(Su.sun_intensity, &fx.sun_intensity, 0.f, 10.f);
          imgui.SliderFloat(Su.ambient_scale, &fx.ambient_scale, 0.f, 3.f);
          imgui.SliderFloat(Su.exposure, &fx.exposure, 0.2f, 3.f);
          imgui.SliderInt(Su.tonemap_mode, &fx.tonemap_mode, 0, 2);
          imgui.SliderFloat(Su.ssr_intensity, &fx.ssr_intensity, 0.f, 1.5f);
          imgui.SliderFloat(Su.dof_focus, &fx.dof_focus, 1.f, 40.f);
          imgui.SliderFloat(Su.dof_scale, &fx.dof_scale, 0.f, 0.3f);
          imgui.SliderFloat(Su.motion_blur_strength, &fx.motion_blur_strength, 0.f, 0.9f);
          imgui.SliderFloat(Su.bloom_thr, &fx.bloom_threshold, 0.35f, 2.f);
          imgui.SliderFloat(Su.bloom_int, &fx.bloom_intensity, 0.f, 2.f);
          imgui.SliderFloat(Su.fog_density, &fx.fog_density, 0.f, 0.1f);
          imgui.SliderFloat(Su.fog_start, &fx.fog_start, 0.f, 40.f);
          imgui.SliderFloat(Su.shadow_bias, &fx.shadow_bias, 0.0001f, 0.02f);
          imgui.SliderFloat(Su.specular_power, &fx.specular_power, 1.f, 128.f);
          imgui.SliderFloat(Su.local_light_scale, &fx.local_intensity_scale, 0.f, 4.f);
          imgui.SliderFloat(Su.ibl_intensity, &fx.ibl_intensity, 0.f, 2.f);
          imgui.SliderFloat(Su.reflection_intensity, &fx.reflection_intensity, 0.f, 1.5f);
          imgui.SliderInt(Su.shadow_cascades, &fx.shadow_cascades, 1, 4);
          imgui.Separator();
          auto apply_quality_tier = [&](engine::render::QualityTier tier) {
            const auto q = engine::render::QualitySettings::FromTier(tier);
            fx.shadow_cascades = q.shadow_cascades;
            fx.enable_ssao = q.enable_ssao;
            // Keep TAA as user checkbox; tier High enabling it causes Halton shake.
            fx.enable_ssr = q.enable_ssr;
            fx.enable_bloom = false;
            render.set_quality(q);
            render.set_effect_tuning(fx);
            veg_density_cap = veg_cap_for_tier(tier);
            engine::LogInfo("QualityTier veg_density_cap=" + std::to_string(veg_density_cap));
          };
          if (imgui.Button(Su.quality_low, 90.f, 0.f)) {
            apply_quality_tier(engine::render::QualityTier::Low);
          }
          if (imgui.Button(Su.quality_med, 90.f, 0.f)) {
            apply_quality_tier(engine::render::QualityTier::Medium);
          }
          if (imgui.Button(Su.quality_high, 90.f, 0.f)) {
            apply_quality_tier(engine::render::QualityTier::High);
          }
          imgui.Separator();
          if (imgui.Button(Su.quit, 80.f, 0.f)) {
            app_ref.window().RequestClose();
          }
        }
        imgui.EndWindow();
      } else {
        if (imgui.BeginWindow(S.hint, 16.f, 16.f, 320.f, 110.f)) {
          char line[128];
          std::snprintf(line, sizeof(line), "%.0f FPS | %.1f ms | CPU %.0f%% | WS %.0f MB",
                        perf.fps, perf.frame_ms, perf.cpu_percent, perf.working_set_mb);
          imgui.Text(line);
          imgui.Text(S.hint_keys);
          if (record_png) {
            char rec[96];
            std::snprintf(rec, sizeof(rec), "%s #%d", S.record_on, record_frame_index);
            imgui.Text(rec);
          }
        }
        imgui.EndWindow();
      }

      if (profiler_open) {
        if (imgui.BeginWindow(S.profiler, 370.f, 48.f, 320.f, 320.f)) {
          char line[160];
          std::snprintf(line, sizeof(line), "1s avg  FPS %.1f | dt %.2f ms | CPU %.1f%%", perf.fps,
                        perf.frame_ms, perf.cpu_percent);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "Mem WS %.1f / Priv %.1f / Peak %.1f MB",
                        perf.working_set_mb, perf.private_mb, perf.peak_working_set_mb);
          imgui.Text(line);
          imgui.Separator();
          imgui.Text(S.cpu_scopes);
          for (const auto& [name, ms] : displayed_cpu_scopes) {
            std::snprintf(line, sizeof(line), "  %s: %.3f ms", name.c_str(), ms);
            imgui.Text(line);
          }
          imgui.Separator();
          imgui.Text("GPU (1s snapshot)");
          if (displayed_gpu_passes.empty()) {
            imgui.Text("  (n/a on this backend)");
          } else {
            for (const auto& t : displayed_gpu_passes) {
              std::snprintf(line, sizeof(line), "  %s: %.3f ms", t.name.c_str(), t.ms);
              imgui.Text(line);
            }
          }
        }
        imgui.EndWindow();
      }

      imgui.RefreshCapture();
      app_ref.set_ui_want_capture(imgui.want_capture_mouse() || imgui.want_capture_keyboard());
    } else {
      app_ref.set_ui_want_capture(false);
    }
    render.set_effect_tuning(fx);

    // C05/W7: optional atmosphere (+ cloud band) coupled into fog / clear.
    if (env.enable_atmosphere) {
      engine::render::AtmosphereParams ap;
      ap.sun_dir = env.sun_direction;
      ap.turbidity = 2.5f;
      const auto& cam = app_ref.camera();
      const engine::Quat q = engine::Quat::FromEulerYxz(cam.yaw, cam.pitch, 0.f);
      const engine::Vec3 fwd = q.Rotate(engine::Vec3{0.f, 0.f, -1.f});
      const auto coupled = engine::render::CoupleFogWithAtmosphere(
          ap, fwd, fx.fog_density > 1e-6f ? fx.fog_density : 0.02f, env.enable_volume_clouds);
      env.fog_color = coupled.fog_color;
      env.clear_color = coupled.clear_color;
      fx.fog_color = {coupled.fog_color.r, coupled.fog_color.g, coupled.fog_color.b};
      if (fx.enable_fog) {
        fx.fog_density = coupled.fog_density;
      }
      app_ref.set_clear_color(env.clear_color);
      render.set_effect_tuning(fx);
    }

    // C02 optional: CPU Forward+ tile lists (8x4) for debug / future cluster path.
    {
      std::vector<std::vector<int>> tiles;
      const float aspect_tile = dh > 0.f ? dw / dh : (16.f / 9.f);
      engine::render::AssignLightsToTiles(sandbox_local_lights,
                                          app_ref.camera().view_proj_matrix(aspect_tile),
                                          engine::render::kLightTileGridW,
                                          engine::render::kLightTileGridH, tiles);
      (void)tiles;
    }

    // M22 / W-gi-deepen: probe irradiance → ambient tint (additive over base;
    // IBL still applied in lit shader when enable_ibl). Does not replace sky/IBL/Lightmap.
    probes.set_enabled(enable_gi);
    if (enable_gi) {
      engine::gi::ProbeLight pl;
      pl.position = {1.8f, 2.8f, 1.0f};
      pl.color = {1.f, 0.78f, 0.55f, 1.f};
      pl.intensity = 1.35f;
      pl.range = 5.5f;
      probes.TickProduct({&pl, 1}, 0.18f);
      // W17: sample via CPU irradiance atlas (same trilinear as Sample).
      const auto atlas = probes.BuildIrradianceAtlasCpu();
      const auto irr = probes.SampleAtlasCpu(atlas, app_ref.camera().position);
      env.ambient = {0.12f + irr.r * 0.35f, 0.13f + irr.g * 0.35f, 0.15f + irr.b * 0.35f, 1.f};
    } else {
      env.ambient = {0.20f, 0.21f, 0.24f, 1.f};
    }
    // Keep lightmap multiply in sync if toggled via harness/console later.
    sync_lightmap_albedo(enable_lightmap);

    // M7 particles near lamp (frozen under gpu-headless assert for Q1 determinism).
    if (!gpu_headless_assert) {
      particles.set_origin({1.8f, 2.6f, 1.0f});
      particles.Step(app_ref.delta_time());
      gpu_particles.set_origin({1.8f, 2.6f, 1.0f});
      (void)gpu_particles.Step(app_ref.delta_time());
      // W16: VT near-field when Feature virtual_texture / vt_near_default on.
      if (engine::QueryFeature("virtual_texture") || engine::QueryFeature("vt_near_default")) {
        engine::vt::VtFeedbackRequest fb{};
        fb.page = sandbox_vt.UvToPage(0.5f, 0.5f, 0);
        fb.importance = 1.f;
        (void)sandbox_vt.TickNearField({&fb, 1}, 4);
      }
      if (large_terrain_mode) {
        terrain_chunks.Update(app_ref.camera().position, terrain_budget);
      }
      // Thin TrailRibbon: orbit the lamp for DebugDraw segments Sandbox can call.
      const float t = static_cast<float>(app_ref.frame_index()) * 0.05f;
      lamp_trail.Push({1.8f + std::cos(t) * 0.35f, 2.6f + 0.15f * std::sin(t * 1.7f),
                       1.0f + std::sin(t) * 0.35f});
      lamp_trail.Step(app_ref.delta_time());
      lamp_trail.AppendDebugLines(dbg);
    }

    // M14 morph upload only when weights change (avoid per-frame GPU buffer destroy).
    if ((std::fabs(morph_w0 - morph_w0_uploaded) > 1e-4f ||
         std::fabs(morph_w1 - morph_w1_uploaded) > 1e-4f)) {
      std::vector<engine::Vec3> morphed;
      engine::animation::ApplyMorphTargets(morph_bind, morph_targets, {morph_w0, morph_w1},
                                           morphed);
      static const int faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
                                       {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7},
                                       {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};
      std::vector<engine::rhi::LitVertex> mverts;
      std::vector<std::uint32_t> minds;
      mverts.reserve(36);
      minds.reserve(36);
      for (const auto& f : faces) {
        const auto& a0 = morphed[static_cast<std::size_t>(f[0])];
        const auto& a1 = morphed[static_cast<std::size_t>(f[1])];
        const auto& a2 = morphed[static_cast<std::size_t>(f[2])];
        const engine::Vec3 n = engine::Normalize(engine::Cross(a1 - a0, a2 - a0));
        const std::uint32_t base = static_cast<std::uint32_t>(mverts.size());
        mverts.push_back({a0.x, a0.y, a0.z, n.x, n.y, n.z, 0, 0});
        mverts.push_back({a1.x, a1.y, a1.z, n.x, n.y, n.z, 1, 0});
        mverts.push_back({a2.x, a2.y, a2.z, n.x, n.y, n.z, 0, 1});
        minds.push_back(base);
        minds.push_back(base + 1);
        minds.push_back(base + 2);
      }
      if (auto st = app_ref.device().UploadLitGeometry(3, mverts, minds); st) {
        morph_w0_uploaded = morph_w0;
        morph_w1_uploaded = morph_w1;
      }
    }

    // M10 LOD + M23 QualityTier density: hide far / over-cap vegetation
    {
      const std::vector<float> ranges{8.f, 16.f, 28.f};
      const auto cam = app_ref.camera().position;
      for (std::size_t i = 0; i < veg_nodes.size(); ++i) {
        const auto p = app_ref.world().world_matrix(veg_nodes[i]).TransformPoint({0, 0, 0});
        const float d = (p - cam).length();
        const int level = engine::assets::LodSelect::SelectLevel(d, ranges);
        const bool in_density = i < veg_density_cap;
        app_ref.world().set_visible(veg_nodes[i], in_density && level < 3);
      }
    }

    const bool mouse_pressed = snap.mouse_left && !mouse_left_was;
    const bool mouse_released = !snap.mouse_left && mouse_left_was;
    mouse_left_was = snap.mouse_left;
    if (mouse_pressed && !app_ref.ui_want_capture()) {
      pick_tracking = true;
      pick_press_x = snap.mouse_x;
      pick_press_y = snap.mouse_y;
    }
    render.set_effect_tuning(fx);

    // M16 sprites + M7 particle screen proxies
    sprites.clear();
    {
      // Distinct warm marker (not cyan — cyan matched the washed floor bug).
      engine::render2d::Sprite s;
      s.position = {dw - 120.f, 16.f};
      s.size = {96.f, 48.f};
      s.color = {0.95f, 0.55f, 0.15f, 0.9f};
      s.sort_y = s.position.y;
      sprites.push_back(s);
    }
    const float aspect_pre = dh > 0.f ? dw / dh : 1.f;
    const auto vp = app_ref.camera().view_proj_matrix(aspect_pre);
    // Cap on-screen particle sprites to keep ScreenQuad load bounded.
    int particle_sprites = 0;
    for (const auto& p : particles.particles()) {
      if (particle_sprites >= 48) {
        break;
      }
      engine::Vec4 c{p.position.x, p.position.y, p.position.z, 1.f};
      float x = vp.m[0] * c.x + vp.m[4] * c.y + vp.m[8] * c.z + vp.m[12] * c.w;
      float y = vp.m[1] * c.x + vp.m[5] * c.y + vp.m[9] * c.z + vp.m[13] * c.w;
      float w = vp.m[3] * c.x + vp.m[7] * c.y + vp.m[11] * c.z + vp.m[15] * c.w;
      if (w <= 1e-4f) {
        continue;
      }
      x /= w;
      y /= w;
      if (x < -1.f || x > 1.f || y < -1.f || y > 1.f) {
        continue;
      }
      engine::render2d::Sprite s;
      s.position = {(x * 0.5f + 0.5f) * dw - p.size * 0.5f,
                    (1.f - (y * 0.5f + 0.5f)) * dh - p.size * 0.5f};
      s.size = {p.size, p.size};
      s.color = p.color;
      s.sort_y = s.position.y;
      sprites.push_back(s);
      ++particle_sprites;
    }
    engine::render2d::SortSprites(sprites);

    const float aspect = dh > 0.f ? dw / dh : 1.f;
    // Character / possess writes local transforms above — refresh world matrices
    // before Extract (Application already ran UpdateTransforms earlier this frame).
    app_ref.world().UpdateTransforms();
    const auto scene = engine::render::RenderSceneExtractor::Extract(
        app_ref.world(), app_ref.camera(), aspect);

    // M20 pick on click-release without drag (LMB look must not fight pick).
    if (mouse_released && pick_tracking && !app_ref.ui_want_capture()) {
      const float dx = snap.mouse_x - pick_press_x;
      const float dy = snap.mouse_y - pick_press_y;
      if (dx * dx + dy * dy < 16.f * 16.f) {
        engine::mixed::PickQuery pq;
        pq.screen_px = {pick_press_x, pick_press_y};
        pq.viewport_w = dw;
        pq.viewport_h = dh;
        if (pq.viewport_w > 1.f && pq.viewport_h > 1.f) {
          pq.inv_view_proj = scene.camera.view_proj_matrix(aspect).Inverse();
          const auto hit = engine::mixed::Pick(scene.instances, sprites, pq);
          if (hit.kind == engine::mixed::PickHit::Kind::Scene3D &&
              app_ref.world().valid(hit.node)) {
            picked_node = hit.node;
          } else {
            picked_node = engine::scene::kInvalidNode;
          }
        }
      }
    }
    if (mouse_released || !snap.mouse_left) {
      pick_tracking = false;
    }

    profiler.Begin("DrawFrame");
    // Scale path BEFORE DrawFrame so instancing runs in OpaqueLit (pre-post).
    if (!gpu_headless_assert && show_scale_instances) {
      std::vector<engine::Mat4> visible;
      engine::gpu_driven::IndirectDrawArgs iargs{};
      const auto scale_vp = scene.camera.view_proj_matrix(aspect);
      const std::uint32_t kept = engine::gpu_driven::CullInstancesToIndirect(
          scale_worlds, {}, scale_vp, &scale_occ, visible, iargs, 36);
      if (kept > 0) {
        (void)app_ref.device().UploadInstanceTransforms(visible);
        engine::rhi::LitDrawItem proto{};
        proto.color = {0.35f, 0.55f, 0.32f, 1.f};
        proto.metallic = 0.05f;
        proto.roughness = 0.7f;
        proto.use_albedo = false;
        render.SetPendingLitInstanced(proto, visible);
        // Seed IndirectArgs (instance_count=0); Cull CS writes instance_count via UAV.
        // Default draw stays DrawLitInstanced (CPU kept count); ExecuteIndirect is available.
        engine::gpu_driven::IndirectDrawArgs seed = iargs;
        seed.instance_count = 0;
        const auto packed = engine::gpu_driven::PackIndirectArgsU32(seed);
        (void)app_ref.device().UploadIndirectIndexedArgs(packed);
        std::uint32_t gpu_vis = kept;
        (void)app_ref.device().DispatchInstanceCull(scale_vp, kept, gpu_vis);
      }
      if ((app_ref.frame_index() % 60) == 0) {
        engine::LogInfo("scale instances visible=" + std::to_string(kept) + "/" +
                        std::to_string(kScaleInstances) +
                        " hiz=" + (engine::QueryFeature("hiz") ? "1" : "0"));
      }
    }
    // GPU probe writes dedicated probe cube (independent of IBL prefilter).
    if ((app_ref.frame_index() % 64) == 1 && !use_vulkan && !gpu_headless_assert) {
      std::vector<engine::rhi::LitDrawItem> probe_items;
      probe_items.reserve(8);
      for (const auto& inst : scene.instances) {
        engine::rhi::LitDrawItem item{};
        item.world = inst.world;
        item.color = {0.75f, 0.75f, 0.78f, 1.f};
        item.mesh_slot = 0;
        probe_items.push_back(item);
        if (probe_items.size() >= 8) {
          break;
        }
      }
      if (!probe_items.empty()) {
        if (auto st = app_ref.device().CaptureReflectionProbeGpu(
                reflection_probe.position(), reflection_probe.face_size(), probe_items);
            !st) {
          std::vector<engine::gi::SceneCaptureOrb> orbs;
          orbs.push_back({{1.5f, 1.2f, 0.f}, {1.f, 0.4f, 0.2f, 1.f}, 0.8f});
          std::vector<std::uint8_t> faces;
          engine::gi::CaptureApproximateSceneFaces(faces, reflection_probe.face_size(),
                                                   reflection_probe.position(), orbs,
                                                   env.sun_direction, env.sun_color, fx.sun_intensity,
                                                   env.ambient);
          if (!faces.empty()) {
            (void)app_ref.device().UploadReflectionCubemap(faces.data(),
                                                           reflection_probe.face_size());
          }
        }
      }
      reflection_probe.ClearDirty();
    }
    if (auto st = render.DrawFrame(app_ref.device(), scene, env, aspect, &sprites, nullptr, &dbg);
        !st) {
      engine::LogError(st.message());
    }
    // (scale path moved above DrawFrame — must not DrawLitInstanced after post)
    if (gpu_headless_assert) {
      const int frames = desc.headless_frames > 0 ? desc.headless_frames : 1;
      if (app_ref.frame_index() + 1 >= static_cast<std::uint64_t>(frames)) {
      std::vector<std::uint8_t> rgba;
      int rw = 0;
      int rh = 0;
      if (auto st = app_ref.device().ReadbackTextureStub(rgba, rw, rh); !st) {
        engine::LogError(st.message());
        headless_assert_failed = true;
        app_ref.window().RequestClose();
      } else {
        bool all_black = true;
        bool all_white = true;
        for (std::size_t i = 0; i + 2 < rgba.size(); i += 4) {
          if (rgba[i] > 2 || rgba[i + 1] > 2 || rgba[i + 2] > 2) {
            all_black = false;
          }
          if (rgba[i] < 250 || rgba[i + 1] < 250 || rgba[i + 2] < 250) {
            all_white = false;
          }
        }
        if (rw <= 0 || rh <= 0 || all_black || all_white) {
          engine::LogError("gpu-headless readback assertion failed (blank/white frame)");
          headless_assert_failed = true;
          app_ref.window().RequestClose();
        }
        if (const char* dump = std::getenv("ENGINE_GOLDEN_DUMP")) {
          if (dump[0] != '\0' && rw > 0 && rh > 0 &&
              rgba.size() >= static_cast<std::size_t>(rw * rh * 4)) {
            std::ofstream out(dump, std::ios::binary);
            const std::uint32_t w32 = static_cast<std::uint32_t>(rw);
            const std::uint32_t h32 = static_cast<std::uint32_t>(rh);
            out.write(reinterpret_cast<const char*>(&w32), 4);
            out.write(reinterpret_cast<const char*>(&h32), 4);
            out.write(reinterpret_cast<const char*>(rgba.data()),
                      static_cast<std::streamsize>(w32 * h32 * 4));
          }
        }
        if (const char* dump_d = std::getenv("ENGINE_GOLDEN_DUMP_DEPTH")) {
          if (dump_d[0] != '\0') {
            std::vector<std::uint8_t> depth_rgba;
            int dw = 0;
            int dh = 0;
            if (auto dst = app_ref.device().ReadbackDepthRgbaStub(depth_rgba, dw, dh);
                dst && dw > 0 && dh > 0 &&
                depth_rgba.size() >= static_cast<std::size_t>(dw * dh * 4)) {
              std::ofstream out(dump_d, std::ios::binary);
              const std::uint32_t w32 = static_cast<std::uint32_t>(dw);
              const std::uint32_t h32 = static_cast<std::uint32_t>(dh);
              out.write(reinterpret_cast<const char*>(&w32), 4);
              out.write(reinterpret_cast<const char*>(&h32), 4);
              out.write(reinterpret_cast<const char*>(depth_rgba.data()),
                        static_cast<std::streamsize>(w32 * h32 * 4));
            } else if (!dst) {
              engine::LogError(dst.message());
            }
          }
        }
      }
      }
    }
    if (harness_stdio) {
      std::string line;
      if (!std::getline(std::cin, line)) {
        app_ref.window().RequestClose();
      } else {
        engine::debug::HarnessCommand hcmd;
        std::string herr;
        if (!engine::debug::ParseHarnessLine(line, hcmd, herr)) {
          std::cout << engine::debug::HarnessErr(herr.empty() ? "parse" : herr) << std::endl;
        } else if (hcmd.cmd == "quit") {
          std::cout << engine::debug::HarnessOk() << std::endl;
          app_ref.window().RequestClose();
        } else if (hcmd.cmd == "ping") {
          std::cout << engine::debug::HarnessOk("\"pong\":true") << std::endl;
        } else if (hcmd.cmd == "query_features") {
          std::cout << engine::debug::HarnessOk(
                           std::string("\"gpu_instancing\":") +
                           (engine::QueryFeature("gpu_instancing") ? "true" : "false") +
                           ",\"execute_indirect\":" +
                           (engine::QueryFeature("execute_indirect") ? "true" : "false"))
                    << std::endl;
        } else if (hcmd.cmd == "camera") {
          app_ref.camera().position = {hcmd.fx, hcmd.fy, hcmd.fz};
          std::cout << engine::debug::HarnessOk() << std::endl;
        } else if (hcmd.cmd == "capture") {
          std::vector<std::uint8_t> rgba;
          int rw = 0, rh = 0;
          if (auto st = app_ref.device().ReadbackTextureStub(rgba, rw, rh); st && !hcmd.key.empty()) {
            std::ofstream out(hcmd.key, std::ios::binary);
            const std::uint32_t w32 = static_cast<std::uint32_t>(rw);
            const std::uint32_t h32 = static_cast<std::uint32_t>(rh);
            out.write(reinterpret_cast<const char*>(&w32), 4);
            out.write(reinterpret_cast<const char*>(&h32), 4);
            out.write(reinterpret_cast<const char*>(rgba.data()),
                      static_cast<std::streamsize>(w32 * h32 * 4));
            std::cout << engine::debug::HarnessOk("\"path\":\"" + hcmd.key + "\"") << std::endl;
          } else {
            std::cout << engine::debug::HarnessErr("capture failed") << std::endl;
          }
        } else if (hcmd.cmd == "toggle") {
          bool* flag = nullptr;
          if (hcmd.key == "taa") {
            flag = &fx.enable_taa;
          } else if (hcmd.key == "ssao") {
            flag = &fx.enable_ssao;
          } else if (hcmd.key == "ibl") {
            flag = &fx.enable_ibl;
          } else if (hcmd.key == "shadows") {
            flag = &fx.enable_shadows;
          } else if (hcmd.key == "ssr") {
            flag = &fx.enable_ssr;
          } else if (hcmd.key == "bloom") {
            flag = &fx.enable_bloom;
          } else if (hcmd.key == "fog") {
            flag = &fx.enable_fog;
          } else if (hcmd.key == "reflection" || hcmd.key == "probe") {
            flag = &fx.enable_reflection_probe;
          }
          if (!flag) {
            std::cout << engine::debug::HarnessErr("unknown toggle key") << std::endl;
          } else {
            *flag = !*flag;
            render.set_effect_tuning(fx);
            std::cout << engine::debug::HarnessOk(std::string("\"key\":\"") + hcmd.key +
                                                 "\",\"value\":" + (*flag ? "true" : "false"))
                      << std::endl;
          }
        } else if (hcmd.cmd == "set_quality") {
          engine::render::QualityTier tier = engine::render::QualityTier::Medium;
          if (hcmd.key == "low") {
            tier = engine::render::QualityTier::Low;
          } else if (hcmd.key == "high") {
            tier = engine::render::QualityTier::High;
          } else if (hcmd.key == "medium" || hcmd.key == "med") {
            tier = engine::render::QualityTier::Medium;
          }
          const auto q = engine::render::QualitySettings::FromTier(tier);
          if (!(desc.gpu_headless || harness_stdio)) {
            fx.shadow_cascades = q.shadow_cascades;
            fx.enable_ssao = q.enable_ssao;
            fx.enable_taa = q.enable_taa;
            fx.enable_ssr = q.enable_ssr;
            fx.enable_bloom = q.enable_bloom;
          }
          render.set_quality(q);
          render.set_effect_tuning(fx);
          veg_density_cap = veg_cap_for_tier(tier);
          std::cout << engine::debug::HarnessOk(std::string("\"tier\":\"") + hcmd.key +
                                               "\",\"veg_cap\":" + std::to_string(veg_density_cap))
                    << std::endl;
        } else if (hcmd.cmd == "frame") {
          // Frame already advanced by Application::Run; acknowledge step count.
          std::cout << engine::debug::HarnessOk(
                           std::string("\"frame\":") + std::to_string(app_ref.frame_index()))
                    << std::endl;
        } else if (hcmd.cmd == "profiler_snapshot") {
          std::string samples = "\"cpu\":[";
          bool first = true;
          for (const auto& [name, ms] : profiler.samples_ms()) {
            if (!first) {
              samples += ",";
            }
            first = false;
            samples += "{\"name\":\"" + name + "\",\"ms\":" + std::to_string(ms) + "}";
          }
          samples += "]";
          std::cout << engine::debug::HarnessOk(samples) << std::endl;
        } else {
          std::cout << engine::debug::HarnessErr("unknown cmd") << std::endl;
        }
      }
    }
    profiler.End("DrawFrame");
    if (imgui_ready) {
      profiler.Begin("ImGui");
      if (auto st = imgui.Render(app_ref.device()); !st) {
        engine::LogError(st.message());
      }
      profiler.End("ImGui");
    }

    // ~60 Hz backbuffer → BMP (async write; GPU readback still sync).
    if (record_png && record_writer && !gpu_headless_assert) {
      const auto now = std::chrono::steady_clock::now();
      constexpr auto kRecordInterval = std::chrono::duration<double>(1.0 / 60.0);
      const bool due = record_last_sample.time_since_epoch().count() == 0 ||
                       (now - record_last_sample) >= kRecordInterval;
      if (due) {
        std::vector<std::uint8_t> rgba;
        int rw = 0;
        int rh = 0;
        if (auto st = app_ref.device().ReadbackTextureStub(rgba, rw, rh); !st) {
          record_last_error = st.message();
          engine::LogError(std::string("Record BMP readback: ") + st.message());
        } else if (rw > 0 && rh > 0 &&
                   rgba.size() >= static_cast<std::size_t>(rw) * static_cast<std::size_t>(rh) * 4u) {
          char name[64];
          std::snprintf(name, sizeof(name), "frame_%04d.bmp", record_frame_index);
          const auto path = record_dir / name;
          if (record_writer->Enqueue(path.string(), rw, rh, std::move(rgba))) {
            ++record_frame_index;
            record_last_sample = now;
            record_last_path = path.string();
            record_last_error.clear();
          } else {
            record_last_sample = now;
            record_last_error = "queue full (dropped)";
          }
        } else {
          record_last_error = "empty readback";
        }
      }
    }

    profiler.End("Frame");
  });
  if (record_writer) {
    record_writer.reset();
  }
  if (!status || headless_assert_failed) {
    return 1;
  }
  return 0;
}
