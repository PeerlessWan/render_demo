#include "engine/app/application.h"

#include "engine/animation/skeleton.h"
#include "engine/assets/gltf_loader.h"
#include "engine/assets/image_loader.h"
#include "engine/assets/streaming_budget.h"
#include "engine/core/log.h"
#include "engine/debug/console.h"
#include "engine/debug/sandbox_harness.h"
#include "engine/core/feature.h"
#include "engine/gi/probe_volume.h"
#include "engine/gi/reflection_probe.h"
#include "engine/gi/scene_capture.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/render/ibl_pack.h"
#include "engine/render/instance_draw.h"
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
#include "engine/ui/immediate_ui.h"
#include "engine/ui/rml_ui.h"
#include "engine/vfx/particles.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    t.position = {0.f, 1.05f, 0.4f};
    t.scale = {1.15f, 1.15f, 1.15f};
    // Slight turn for a better default silhouette.
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

  engine::render::RenderSystem render;
  engine::render::RenderSystemDesc rdesc;
  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  if (use_vulkan) {
    rdesc.lit_vs = shader_dir / "lit_cube_vk.vs.spv";
    rdesc.lit_ps = shader_dir / "lit_cube_vk.ps.spv";
    rdesc.shadow_vs = shader_dir / "shadow_vk.vs.spv";
    rdesc.post_vs = shader_dir / "post_tonemap_vk.vs.spv";
    rdesc.post_ps = shader_dir / "post_tonemap_vk.ps.spv";
    rdesc.quad_vs = shader_dir / "quad_vk.vs.spv";
    rdesc.quad_ps = shader_dir / "quad_vk.ps.spv";
    rdesc.debug_vs = shader_dir / "debug_line_vk.vs.spv";
    rdesc.debug_ps = shader_dir / "debug_line_vk.ps.spv";
    rdesc.sky_vs = shader_dir / "skybox_vk.vs.spv";
    rdesc.sky_ps = shader_dir / "skybox_vk.ps.spv";
    rdesc.enable_shadows = true;
    rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
    rdesc.quality.enable_ssao = false;
    // Halton clip jitter without a rock-solid history resolve crawls on large floors /
    // thin green scale pillars. Keep High shadows; TAA stays opt-in via ImGui.
    rdesc.quality.enable_taa = false;
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
    // Halton clip jitter without a rock-solid history resolve crawls on large floors /
    // thin green scale pillars. Keep High shadows; TAA stays opt-in via ImGui.
    rdesc.quality.enable_taa = false;
  }
  if (auto st = render.Init(a.device(), rdesc); !st) {
    engine::LogError(st.message());
    return 1;
  }
  render.ApplyEnvironmentDefaults(env);
  if (!use_vulkan) {
    const auto cull_cs = shader_dir / "instance_cull_cs.cso";
    if (auto st = a.device().SetupInstanceCullCompute(cull_cs); !st) {
      engine::LogWarn(std::string("Cull CS optional: ") + st.message());
    }
    if (auto st = a.device().ProbeBindlessMinimalPath(0); !st) {
      engine::LogWarn(std::string("Bindless Feature path: ") + st.message());
    } else {
      engine::LogInfo("Bindless Feature minimal path OK");
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
      if (auto st = a.device().UploadLitAlbedoRgba(alb->rgba.data(), alb->width, alb->height, 0);
          !st) {
        engine::LogError(st.message());
        return 1;
      } else {
        engine::LogInfo("Albedo slot0: Poly Haven red_brick_03");
        albedo_ok = true;
      }
    } else if (auto alb_fallback = loader->LoadFile(content / "textures" / "albedo_brick.png")) {
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
  }
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
    render.set_local_lights(lights);
  }

  engine::render::EffectTuning fx = render.effect_tuning();
  fx.sun_intensity = env.sun_intensity;
  fx.ambient_scale = 1.05f;
  fx.exposure = use_vulkan ? 1.f : 1.0f;
  fx.enable_ssao = rdesc.quality.enable_ssao;
  fx.enable_taa = rdesc.quality.enable_taa;
  fx.enable_ssr = rdesc.quality.enable_ssr;
  fx.enable_bloom = false;  // Bloom turns the horizon band into a floating white slab on LDR.
  fx.bloom_threshold = 1.1f;
  fx.bloom_intensity = 0.2f;
  fx.enable_auto_exposure = false;
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

  // M23: small heightfield patch + vegetation cubes
  engine::terrain::Heightmap heightmap;
  heightmap.width = 17;
  heightmap.height = 17;
  heightmap.cell = 0.75f;
  heightmap.samples.resize(static_cast<std::size_t>(17 * 17));
  for (int z = 0; z < 17; ++z) {
    for (int x = 0; x < 17; ++x) {
      const float nx = (x - 8) * 0.22f;
      const float nz = (z - 8) * 0.22f;
      heightmap.samples[static_cast<std::size_t>(z * 17 + x)] =
          0.35f * std::sin(nx * 2.1f) * std::cos(nz * 1.7f);
    }
  }
  const auto terrain_mesh =
      engine::terrain::BuildTerrainMesh(heightmap, {-22.f, -0.35f, -22.f});
  if (!terrain_mesh.indices.empty()) {
    std::vector<engine::rhi::LitVertex> tverts(terrain_mesh.positions.size() / 3);
    for (std::size_t i = 0; i < tverts.size(); ++i) {
      tverts[i] = {terrain_mesh.positions[i * 3 + 0], terrain_mesh.positions[i * 3 + 1],
                   terrain_mesh.positions[i * 3 + 2], terrain_mesh.normals[i * 3 + 0],
                   terrain_mesh.normals[i * 3 + 1], terrain_mesh.normals[i * 3 + 2],
                   terrain_mesh.uvs[i * 2 + 0], terrain_mesh.uvs[i * 2 + 1]};
    }
    if (auto st = a.device().UploadLitGeometry(2, tverts, terrain_mesh.indices); st) {
      auto terrain_node = a.world().CreateNode("terrain");
      engine::scene::Transform tt;
      tt.position = {0, 0, 0};
      a.world().set_local_transform(terrain_node, tt);
      engine::scene::MeshRenderer tm;
      tm.mesh_id = "terrain";
      tm.never_cull = true;
      // Keep terrain patch away from the main ±12 ground plane to avoid dual-floor confusion.
      tm.local_bounds = {{-22.f, -1.5f, -22.f}, {-22.f + 16.f * 0.75f, 2.f, -22.f + 16.f * 0.75f}};
      a.world().set_mesh(terrain_node, tm);
      engine::LogInfo("Terrain heightmap mesh uploaded (slot2)");
    }
  }
  const auto veg = engine::terrain::ScatterVegetation(heightmap, 0.05f, 4);
  std::vector<engine::scene::NodeId> veg_nodes;
  for (std::size_t i = 0; i < veg.size() && i < 24; ++i) {
    auto id = a.world().CreateNode("veg" + std::to_string(i));
    engine::scene::Transform t;
    t.position = {-22.f + veg[i].position.x, -0.35f + veg[i].position.y + 0.4f,
                  -22.f + veg[i].position.z};
    t.scale = {0.25f * veg[i].scale, 0.8f * veg[i].scale, 0.25f * veg[i].scale};
    a.world().set_local_transform(id, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(id, mesh);
    veg_nodes.push_back(id);
  }

  // M6/M14: morph demo mesh (bind cube-ish + smile/frown deltas)
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
    scale_worlds[static_cast<std::size_t>(i)] = engine::Mat4::TRS(
        {static_cast<float>(x) * 0.85f - 13.f, 0.35f, static_cast<float>(z) * 0.85f - 13.f}, {},
        {0.25f, 0.7f, 0.25f});
  }
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

  engine::scene::NodeId picked_node = engine::scene::kInvalidNode;

  bool panel_open = true;
  bool profiler_open = true;
  bool show_grid = true;
  bool show_axes = true;
  bool f1_was_down = false;
  bool f2_was_down = false;
  bool f3_was_down = false;
  bool f4_was_down = false;
  engine::debug::Profiler profiler;
  ProcessPerfSampler process_perf;
  ProcessPerfSnapshot displayed_perf{};
  std::vector<std::pair<std::string, double>> displayed_cpu_scopes;
  std::vector<engine::rhi::GpuPassTiming> displayed_gpu_passes;

  std::vector<engine::render2d::Sprite> sprites;

  auto audio = engine::media::CreateDefaultAudioDevice();
  engine::LogInfo(std::string("Audio backend: ") + audio->backend_name());
  engine::LogInfo("Sandbox: LMB/RMB look | Wheel zoom | MMB pan | F1 FX | F3 grid | F4 axes");

  bool headless_assert_failed = false;
  const auto status = a.Run([&](engine::Application& app_ref) {
    profiler.Begin("Frame");
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

    auto& dbg = app_ref.debug_draw();
    dbg.Clear();
    if (show_grid) {
      // Slightly above the lit ground; darker lines stay visible on bright brick.
      dbg.AddGrid(8.f, 1.f, 0.05f, {0.22f, 0.24f, 0.28f, 1.f}, {0.45f, 0.48f, 0.55f, 1.f});
    }
    if (show_axes) {
      dbg.AddAxes(2.5f, 0.05f);
    }
    for (int bi = 0; bi < physics->body_count(); ++bi) {
      const auto p = physics->body_position(bi);
      const auto he = physics->body_half_extents(bi);
      engine::Aabb box;
      box.min = {p.x - he.x, p.y - he.y, p.z - he.z};
      box.max = {p.x + he.x, p.y + he.y, p.z + he.z};
      dbg.AddAabb(box, {0.2f, 0.95f, 0.35f, 1.f});
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

      // Always-on performance HUD (top-right).
      {
        const float pw = 260.f;
        const float ph = 148.f;
        if (imgui.BeginWindow("Perf", dw - pw - 16.f, 16.f, pw, ph)) {
          char line[160];
          std::snprintf(line, sizeof(line), "FPS  %.1f", perf.fps);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "Frame  %.2f ms", perf.frame_ms);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "CPU  %.1f %%", perf.cpu_percent);
          imgui.Text(line);
          imgui.Separator();
          std::snprintf(line, sizeof(line), "WS  %.1f MB", perf.working_set_mb);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "Private  %.1f MB", perf.private_mb);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "Peak WS  %.1f MB", perf.peak_working_set_mb);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "PageFaults  %u", perf.page_fault_count);
          imgui.Text(line);
        }
        imgui.EndWindow();
      }

      if (panel_open) {
        if (imgui.BeginWindow("Effects", 16.f, 48.f, 360.f, 760.f)) {
          imgui.Text("LMB/RMB drag look | Wheel zoom | MMB pan");
          imgui.Text("WASD/QE | Shift | F1 FX | F2 Profiler | F3 grid");
          imgui.Separator();
          char perf_line[96];
          std::snprintf(perf_line, sizeof(perf_line), "1s avg: %.0f FPS | %.1f ms | CPU %.0f%%",
                        perf.fps, perf.frame_ms, perf.cpu_percent);
          imgui.Text(perf_line);
          imgui.Separator();
          imgui.Checkbox("Show grid (F3)", &show_grid);
          imgui.Checkbox("Show axes (F4)", &show_axes);
          imgui.Checkbox("Probe GI", &enable_gi);
          imgui.SliderFloat("Morph bulge", &morph_w0, 0.f, 1.f);
          imgui.SliderFloat("Morph squash", &morph_w1, 0.f, 1.f);
          imgui.Separator();
          imgui.Checkbox("Shadows", &fx.enable_shadows);
          imgui.Checkbox("SSAO", &fx.enable_ssao);
          imgui.Checkbox("TAA", &fx.enable_taa);
          imgui.Checkbox("IBL", &fx.enable_ibl);
          imgui.Checkbox("Skybox", &fx.enable_skybox);
          imgui.Checkbox("Reflection probe", &fx.enable_reflection_probe);
          imgui.Checkbox("SSR", &fx.enable_ssr);
          imgui.Checkbox("DoF", &fx.enable_dof);
          imgui.Checkbox("MotionBlur", &fx.enable_motion_blur);
          imgui.Checkbox("Tonemap", &fx.enable_tonemap);
          imgui.Checkbox("AutoExposure", &fx.enable_auto_exposure);
          imgui.Checkbox("Bloom", &fx.enable_bloom);
          imgui.Checkbox("Fog", &fx.enable_fog);
          imgui.Separator();
          imgui.SliderFloat("Sun intensity", &fx.sun_intensity, 0.f, 10.f);
          imgui.SliderFloat("Ambient scale", &fx.ambient_scale, 0.f, 3.f);
          imgui.SliderFloat("Exposure", &fx.exposure, 0.2f, 3.f);
          imgui.SliderInt("Tonemap mode", &fx.tonemap_mode, 0, 2);
          imgui.SliderFloat("SSR intensity", &fx.ssr_intensity, 0.f, 1.5f);
          imgui.SliderFloat("DoF focus", &fx.dof_focus, 1.f, 40.f);
          imgui.SliderFloat("DoF scale", &fx.dof_scale, 0.f, 0.3f);
          imgui.SliderFloat("Motion blur", &fx.motion_blur_strength, 0.f, 0.9f);
          imgui.SliderFloat("Bloom thr", &fx.bloom_threshold, 0.35f, 2.f);
          imgui.SliderFloat("Bloom int", &fx.bloom_intensity, 0.f, 2.f);
          imgui.SliderFloat("Fog density", &fx.fog_density, 0.f, 0.1f);
          imgui.SliderFloat("Fog start", &fx.fog_start, 0.f, 40.f);
          imgui.SliderFloat("Shadow bias", &fx.shadow_bias, 0.0001f, 0.02f);
          imgui.SliderFloat("Specular power", &fx.specular_power, 1.f, 128.f);
          imgui.SliderFloat("Local light scale", &fx.local_intensity_scale, 0.f, 4.f);
          imgui.SliderFloat("IBL intensity", &fx.ibl_intensity, 0.f, 2.f);
          imgui.SliderFloat("Reflection intensity", &fx.reflection_intensity, 0.f, 1.5f);
          imgui.SliderInt("Shadow cascades", &fx.shadow_cascades, 1, 4);
          imgui.Separator();
          if (imgui.Button("Low", 90.f, 0.f)) {
            const auto q =
                engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
            fx.enable_ssao = q.enable_ssao;
            fx.enable_taa = q.enable_taa;
            fx.enable_bloom = q.enable_bloom;
            fx.enable_ssr = q.enable_ssr;
            fx.shadow_cascades = q.shadow_cascades;
            render.set_quality(q);
          }
          if (imgui.Button("Med", 90.f, 0.f)) {
            const auto q =
                engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
            fx.enable_ssao = q.enable_ssao;
            fx.enable_taa = q.enable_taa;
            fx.enable_bloom = q.enable_bloom;
            fx.enable_ssr = q.enable_ssr;
            fx.shadow_cascades = q.shadow_cascades;
            render.set_quality(q);
          }
          if (imgui.Button("High", 90.f, 0.f)) {
            const auto q =
                engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
            fx.enable_ssao = q.enable_ssao;
            fx.enable_taa = q.enable_taa;
            fx.enable_bloom = q.enable_bloom;
            fx.enable_ssr = q.enable_ssr;
            fx.shadow_cascades = q.shadow_cascades;
            render.set_quality(q);
          }
          imgui.Separator();
          if (imgui.Button("Quit", 80.f, 0.f)) {
            app_ref.window().RequestClose();
          }
        }
        imgui.EndWindow();
      } else {
        if (imgui.BeginWindow("Hint", 16.f, 16.f, 320.f, 88.f)) {
          char line[128];
          std::snprintf(line, sizeof(line), "%.0f FPS | %.1f ms | CPU %.0f%% | WS %.0f MB",
                        perf.fps, perf.frame_ms, perf.cpu_percent, perf.working_set_mb);
          imgui.Text(line);
          imgui.Text("F1 FX | F2 Profiler | F3 Grid | F4 Axes");
        }
        imgui.EndWindow();
      }

      if (profiler_open) {
        if (imgui.BeginWindow("Profiler", 370.f, 48.f, 320.f, 320.f)) {
          char line[160];
          std::snprintf(line, sizeof(line), "1s avg  FPS %.1f | dt %.2f ms | CPU %.1f%%", perf.fps,
                        perf.frame_ms, perf.cpu_percent);
          imgui.Text(line);
          std::snprintf(line, sizeof(line), "Mem WS %.1f / Priv %.1f / Peak %.1f MB",
                        perf.working_set_mb, perf.private_mb, perf.peak_working_set_mb);
          imgui.Text(line);
          imgui.Separator();
          imgui.Text("CPU scopes (1s)");
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

    // M22 / W-gi-deepen: probe irradiance → ambient tint (additive over base;
    // IBL still applied in lit shader when enable_ibl). Does not replace sky/IBL.
    probes.set_enabled(enable_gi);
    if (enable_gi) {
      engine::gi::ProbeLight pl;
      pl.position = {1.8f, 2.8f, 1.0f};
      pl.color = {1.f, 0.78f, 0.55f, 1.f};
      pl.intensity = 1.35f;
      pl.range = 5.5f;
      probes.UpdateFromLights({&pl, 1});
      const auto irr = probes.Sample(app_ref.camera().position);
      env.ambient = {0.12f + irr.r * 0.35f, 0.13f + irr.g * 0.35f, 0.15f + irr.b * 0.35f, 1.f};
    } else {
      env.ambient = {0.20f, 0.21f, 0.24f, 1.f};
    }

    // M7 particles near lamp (frozen under gpu-headless assert for Q1 determinism).
    if (!gpu_headless_assert) {
      particles.set_origin({1.8f, 2.6f, 1.0f});
      particles.Step(app_ref.delta_time());
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

    // M10 LOD: hide far vegetation
    {
      const std::vector<float> ranges{8.f, 16.f, 28.f};
      const auto cam = app_ref.camera().position;
      for (std::size_t i = 0; i < veg_nodes.size(); ++i) {
        const auto p = app_ref.world().world_matrix(veg_nodes[i]).TransformPoint({0, 0, 0});
        const float d = (p - cam).length();
        const int level = engine::assets::LodSelect::SelectLevel(d, ranges);
        app_ref.world().set_visible(veg_nodes[i], level < 3);
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
    if (!gpu_headless_assert) {
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
        // Vulkan: CPU expand path still honors UploadInstanceTransforms + DrawLitInstanced.
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
          fx.enable_ssao = q.enable_ssao;
          fx.enable_taa = q.enable_taa;
          fx.enable_bloom = q.enable_bloom;
          fx.enable_ssr = q.enable_ssr;
          if (!(desc.gpu_headless || harness_stdio)) {
            fx.shadow_cascades = q.shadow_cascades;
          }
          render.set_quality(q);
          render.set_effect_tuning(fx);
          std::cout << engine::debug::HarnessOk(std::string("\"tier\":\"") + hcmd.key + "\"")
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
    profiler.End("Frame");
  });
  if (!status || headless_assert_failed) {
    return 1;
  }
  return 0;
}
