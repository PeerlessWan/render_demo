#include "engine/app/application.h"

#include "engine/animation/skeleton.h"
#include "engine/assets/gltf_loader.h"
#include "engine/assets/image_loader.h"
#include "engine/assets/streaming_budget.h"
#include "engine/core/log.h"
#include "engine/debug/console.h"
#include "engine/gi/probe_volume.h"
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
#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

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

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc;
  desc.window.title = "Sandbox — LMB/RMB look | Wheel zoom | MMB pan | WASD | F1/F2";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.14f, 0.16f, 0.20f, 1.f};
  bool use_vulkan = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
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
  a.camera().position = {0.f, 1.8f, 5.5f};

  auto ground = a.world().CreateNode("ground");
  {
    engine::scene::Transform t;
    t.position = {0, -0.5f, 0};
    t.scale = {12.f, 1.f, 12.f};
    a.world().set_local_transform(ground, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "ground";
    mesh.never_cull = true;  // large thin floor must not flicker-cull while orbiting
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
    t.position = {2.2f, 1.0f, 1.4f};
    t.scale = {1.1f, 1.1f, 1.1f};
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
  env.sun_direction = {0.45f, -1.f, 0.35f};
  env.sun_intensity = 4.8f;
  env.sun_color = {1.f, 0.97f, 0.92f, 1.f};

  engine::render::RenderSystem render;
  engine::render::RenderSystemDesc rdesc;
  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  if (use_vulkan) {
    rdesc.lit_vs = shader_dir / "lit_cube_vk.vs.spv";
    rdesc.lit_ps = shader_dir / "lit_cube_vk.ps.spv";
    rdesc.shadow_vs = shader_dir / "shadow_vk.vs.spv";
    // Post/UI/debug SPIR-V not wired yet on Vulkan.
    rdesc.enable_shadows = true;
    rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
    rdesc.quality.enable_ssao = false;
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
    rdesc.enable_shadows = true;
    rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  }
  if (auto st = render.Init(a.device(), rdesc); !st) {
    engine::LogError(st.message());
    return 1;
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
        if (use_vulkan) {
          engine::LogInfo(std::string("Skip albedo upload on Vulkan: ") + st.message());
        } else {
          engine::LogError(st.message());
          return 1;
        }
      } else {
        engine::LogInfo("Albedo slot0: Poly Haven red_brick_03");
        albedo_ok = true;
      }
    } else if (auto alb_fallback = loader->LoadFile(content / "textures" / "albedo_brick.png")) {
      if (auto st = a.device().UploadLitAlbedoRgba(alb_fallback->rgba.data(), alb_fallback->width,
                                                    alb_fallback->height, 0);
          !st) {
        if (use_vulkan) {
          engine::LogInfo(std::string("Skip albedo upload on Vulkan: ") + st.message());
        } else {
          engine::LogError(st.message());
          return 1;
        }
      } else {
        engine::LogInfo("Albedo slot0: fallback albedo_brick.png");
        albedo_ok = true;
      }
    }
    if (!albedo_ok && !use_vulkan) {
      engine::LogError("Failed to load any albedo texture");
      return 1;
    }

    bool orm_ok = false;
    if (auto arm = loader->LoadFile(brick_arm)) {
      auto orm = ArmToOrm(arm.value());
      if (auto st = a.device().UploadLitOrmRgba(orm.rgba.data(), orm.width, orm.height, 0); !st) {
        if (use_vulkan) {
          engine::LogInfo(std::string("Skip ORM upload on Vulkan: ") + st.message());
        } else {
          engine::LogError(st.message());
          return 1;
        }
      } else {
        engine::LogInfo("ORM slot0: Poly Haven brick ARM");
        orm_ok = true;
      }
    } else if (auto orm_fallback = loader->LoadFile(content / "textures" / "orm_brick.png")) {
      if (auto st = a.device().UploadLitOrmRgba(orm_fallback->rgba.data(), orm_fallback->width,
                                                orm_fallback->height, 0);
          !st) {
        if (use_vulkan) {
          engine::LogInfo(std::string("Skip ORM upload on Vulkan: ") + st.message());
        } else {
          engine::LogError(st.message());
          return 1;
        }
      } else {
        engine::LogInfo("ORM slot0: fallback orm_brick.png");
        orm_ok = true;
      }
    }
    if (!orm_ok && !use_vulkan) {
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
        if (use_vulkan) {
          engine::LogInfo(std::string("Skip helmet mesh on Vulkan: ") + st.message());
        } else {
          engine::LogError(st.message());
          return 1;
        }
      } else {
        if (mesh->has_albedo) {
          if (auto st = a.device().UploadLitAlbedoRgba(mesh->albedo.rgba.data(), mesh->albedo.width,
                                                       mesh->albedo.height, 1);
              !st) {
            if (use_vulkan) {
              engine::LogInfo(std::string("Skip helmet albedo on Vulkan: ") + st.message());
            } else {
              engine::LogError(st.message());
              return 1;
            }
          }
        }
        if (mesh->has_orm) {
          if (auto st = a.device().UploadLitOrmRgba(mesh->orm.rgba.data(), mesh->orm.width,
                                                   mesh->orm.height, 1);
              !st) {
            if (use_vulkan) {
              engine::LogInfo(std::string("Skip helmet ORM on Vulkan: ") + st.message());
            } else {
              engine::LogError(st.message());
              return 1;
            }
          }
        }
        engine::LogInfo("DamagedHelmet.glb uploaded (mesh slot1 + tex slot1)");
      }
    } else {
      if (use_vulkan) {
        engine::LogInfo(std::string("Helmet load skipped: ") + mesh.status().message());
      } else {
        engine::LogError(std::string("Helmet load failed: ") + mesh.status().message());
        return 1;
      }
    }
  }
  {
    std::vector<engine::render::LocalLight> lights;
    if (!use_vulkan) {
      engine::render::LocalLight lamp;
      lamp.id = 1;
      lamp.position = {1.8f, 2.8f, 1.0f};
      lamp.range = 14.f;
      lamp.color = {1.f, 0.72f, 0.45f, 1.f};
      lamp.intensity = 9.f;
      lamp.shadow_resolution = 512;
      lamp.cast_shadows = true;
      lights.push_back(lamp);
      engine::render::LocalLight cool;
      cool.id = 2;
      cool.position = {-2.2f, 2.6f, 1.8f};
      cool.range = 12.f;
      cool.color = {0.45f, 0.65f, 1.f, 1.f};
      cool.intensity = 7.f;
      cool.shadow_resolution = 512;
      cool.cast_shadows = true;
      lights.push_back(cool);
    }
    render.set_local_lights(lights);
  }

  engine::render::EffectTuning fx = render.effect_tuning();
  fx.sun_intensity = env.sun_intensity;
  fx.ambient_scale = 1.4f;
  fx.exposure = use_vulkan ? 1.f : 1.25f;
  fx.enable_ssao = rdesc.quality.enable_ssao;
  fx.enable_taa = rdesc.quality.enable_taa;
  fx.shadow_cascades = rdesc.quality.shadow_cascades;
  render.set_effect_tuning(fx);

  engine::ui::ImmediateUi imgui;
  bool imgui_ready = false;
  if (!imgui.available()) {
    if (!use_vulkan) {
      engine::LogError("Dear ImGui not available (ENGINE_WITH_IMGUI=0)");
      return 1;
    }
    engine::LogInfo("Dear ImGui not available; continuing without UI panel");
  } else if (!use_vulkan) {
    engine::ui::ImmediateUiDesc ui_desc;
    ui_desc.ui_vs = shader_dir / "ui_imgui.vs.cso";
    ui_desc.ui_ps = shader_dir / "ui_imgui.ps.cso";
    if (auto st = imgui.Init(a.device(), ui_desc); !st) {
      engine::LogError(st.message());
      return 1;
    }
    imgui_ready = true;
  } else {
    engine::LogInfo("ImGui skipped on Vulkan (UI SPIR-V not wired yet)");
  }

  auto physics = engine::physics::CreateDefaultPhysicsWorld();
  engine::LogInfo(std::string("Physics backend: ") + physics->backend_name());
  engine::LogInfo(std::string("Retained UI backend: ") +
                  engine::ui::QueryRetainedUiBackend().name);
  auto retained = engine::ui::CreateRetainedUiBackend();
  bool mouse_left_was = false;
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
      engine::terrain::BuildTerrainMesh(heightmap, {-18.f, -0.2f, -6.f});
  if (!use_vulkan && !terrain_mesh.indices.empty()) {
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
      a.world().set_mesh(terrain_node, tm);
      engine::LogInfo("Terrain heightmap mesh uploaded (slot2)");
    }
  }
  const auto veg = engine::terrain::ScatterVegetation(heightmap, 0.05f, 4);
  std::vector<engine::scene::NodeId> veg_nodes;
  for (std::size_t i = 0; i < veg.size() && i < 24; ++i) {
    auto id = a.world().CreateNode("veg" + std::to_string(i));
    engine::scene::Transform t;
    t.position = {-18.f + veg[i].position.x, -0.2f + veg[i].position.y + 0.4f,
                  -6.f + veg[i].position.z};
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
  auto morph_node = a.world().CreateNode("morph");
  {
    engine::scene::Transform t;
    t.position = {-3.2f, 1.0f, 1.2f};
    a.world().set_local_transform(morph_node, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "morph";
    a.world().set_mesh(morph_node, mesh);
  }

  engine::gi::ProbeVolume probes;
  probes.Configure({-4.f, 0.5f, -4.f}, {2.f, 1.5f, 2.f}, 5, 3, 5);
  bool enable_gi = true;

  engine::vfx::ParticleEmitter particles;
  particles.Configure({1.8f, 2.6f, 1.0f}, 28.f, 1.1f);

  engine::scene::NodeId picked_node = engine::scene::kInvalidNode;

  bool panel_open = true;
  bool profiler_open = false;
  bool show_grid = true;
  bool show_axes = true;
  bool f1_was_down = false;
  bool f2_was_down = false;
  bool f3_was_down = false;
  bool f4_was_down = false;
  engine::debug::Profiler profiler;

  std::vector<engine::render2d::Sprite> sprites;

  auto audio = engine::media::CreateDefaultAudioDevice();
  engine::LogInfo(std::string("Audio backend: ") + audio->backend_name());
  engine::LogInfo("Sandbox: LMB/RMB look | Wheel zoom | MMB pan | F1 FX | F3 grid | F4 axes");

  const auto status = a.Run([&](engine::Application& app_ref) {
    profiler.Begin("Frame");
    physics->Step(app_ref.delta_time());
    {
      engine::scene::Transform t = app_ref.world().local_transform(phys_node);
      t.position = physics->body_position(phys_id);
      app_ref.world().set_local_transform(phys_node, t);
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
      dbg.AddGrid(8.f, 1.f, 0.02f);
    }
    if (show_axes) {
      dbg.AddAxes(2.5f, 0.03f);
    }
    for (int bi = 0; bi < physics->body_count(); ++bi) {
      const auto p = physics->body_position(bi);
      const auto he = physics->body_half_extents(bi);
      engine::Aabb box;
      box.min = {p.x - he.x, p.y - he.y, p.z - he.z};
      box.max = {p.x + he.x, p.y + he.y, p.z + he.z};
      dbg.AddAabb(box, {0.2f, 0.95f, 0.35f, 1.f});
    }
    if (picked_node != engine::scene::kInvalidNode) {
      const auto pos = app_ref.world().world_matrix(picked_node).TransformPoint({0, 0, 0});
      engine::Aabb box;
      box.min = {pos.x - 0.6f, pos.y - 0.6f, pos.z - 0.6f};
      box.max = {pos.x + 0.6f, pos.y + 0.6f, pos.z + 0.6f};
      dbg.AddAabb(box, {1.f, 0.85f, 0.15f, 1.f});
    }

    const float dw = static_cast<float>(app_ref.window().width());
    const float dh = static_cast<float>(app_ref.window().height());
    if (imgui_ready) {
      imgui.BeginFrame(snap, dw, dh, app_ref.delta_time());

      if (panel_open) {
        if (imgui.BeginWindow("Effects", 16.f, 48.f, 360.f, 760.f)) {
          imgui.Text("LMB/RMB drag look | Wheel zoom | MMB pan");
          imgui.Text("WASD/QE | Shift | F1 FX | F3 grid | F4 axes");
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
          imgui.SliderFloat("Bloom thr", &fx.bloom_threshold, 0.2f, 2.f);
          imgui.SliderFloat("Bloom int", &fx.bloom_intensity, 0.f, 2.f);
          imgui.SliderFloat("Fog density", &fx.fog_density, 0.f, 0.1f);
          imgui.SliderFloat("Fog start", &fx.fog_start, 0.f, 40.f);
          imgui.SliderFloat("Shadow bias", &fx.shadow_bias, 0.0001f, 0.02f);
          imgui.SliderFloat("Specular power", &fx.specular_power, 1.f, 128.f);
          imgui.SliderFloat("Local light scale", &fx.local_intensity_scale, 0.f, 4.f);
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
          }
          if (imgui.Button("Med", 90.f, 0.f)) {
            const auto q =
                engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
            fx.enable_ssao = q.enable_ssao;
            fx.enable_taa = q.enable_taa;
            fx.enable_bloom = q.enable_bloom;
            fx.enable_ssr = q.enable_ssr;
            fx.shadow_cascades = q.shadow_cascades;
          }
          if (imgui.Button("High", 90.f, 0.f)) {
            const auto q =
                engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
            fx.enable_ssao = q.enable_ssao;
            fx.enable_taa = q.enable_taa;
            fx.enable_bloom = q.enable_bloom;
            fx.enable_ssr = q.enable_ssr;
            fx.shadow_cascades = q.shadow_cascades;
          }
          imgui.Separator();
          if (imgui.Button("Quit", 80.f, 0.f)) {
            app_ref.window().RequestClose();
          }
        }
        imgui.EndWindow();
      } else {
        if (imgui.BeginWindow("Hint", 16.f, 16.f, 280.f, 72.f)) {
          imgui.Text("F1 FX | F2 Profiler | F3 Grid | F4 Axes");
        }
        imgui.EndWindow();
      }

      if (profiler_open) {
        if (imgui.BeginWindow("Profiler", 370.f, 48.f, 300.f, 280.f)) {
          char line[128];
          std::snprintf(line, sizeof(line), "dt=%.2f ms", app_ref.delta_time() * 1000.f);
          imgui.Text(line);
          imgui.Separator();
          imgui.Text("CPU");
          for (const auto& [name, ms] : profiler.samples_ms()) {
            std::snprintf(line, sizeof(line), "  %s: %.3f ms", name.c_str(), ms);
            imgui.Text(line);
          }
          imgui.Separator();
          imgui.Text("GPU (prev frame)");
          const auto gpu = app_ref.device().LastGpuPassTimings();
          if (gpu.empty()) {
            imgui.Text("  (n/a on this backend)");
          } else {
            for (const auto& t : gpu) {
              std::snprintf(line, sizeof(line), "  %s: %.3f ms", t.name.c_str(), t.ms);
              imgui.Text(line);
            }
          }
        }
        imgui.EndWindow();
      }

      app_ref.set_ui_want_capture(imgui.want_capture_mouse() || imgui.want_capture_keyboard());
    } else {
      app_ref.set_ui_want_capture(false);
    }
    render.set_effect_tuning(fx);

    // M22: probe irradiance → ambient tint
    probes.set_enabled(enable_gi);
    if (enable_gi) {
      const auto irr = probes.Sample(app_ref.camera().position);
      env.ambient = {0.12f + irr.r, 0.13f + irr.g, 0.15f + irr.b, 1.f};
    } else {
      env.ambient = {0.20f, 0.21f, 0.24f, 1.f};
    }

    // M7 particles near lamp
    particles.set_origin({1.8f, 2.6f, 1.0f});
    particles.Step(app_ref.delta_time());

    // M14 morph upload (expanded box faces from 8 corners)
    if (!use_vulkan) {
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
      (void)app_ref.device().UploadLitGeometry(3, mverts, minds);
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
    mouse_left_was = snap.mouse_left;
    {
      const float hx = 16.f;
      const float hy = (std::max)(16.f, dh - 130.f);
      retained->Clear();
      retained->Panel("hud", hx, hy, 220.f, 110.f, {0.06f, 0.07f, 0.1f, 0.82f});
      retained->Label("hud_title", "Retained HUD", hx + 12.f, hy + 12.f);
      retained->Toggle("hud_fog", "Fog", hx + 12.f, hy + 40.f, 180.f, 26.f, fx.enable_fog);
      retained->Toggle("hud_bloom", "Bloom", hx + 12.f, hy + 72.f, 180.f, 26.f, fx.enable_bloom);
    }
    const auto retained_events =
        retained->Pump(snap.mouse_x, snap.mouse_y, snap.mouse_left, mouse_pressed);
    for (const auto& ev : retained_events) {
      if (ev.id == "hud_fog" && ev.type == engine::ui::UiEventType::Toggle) {
        fx.enable_fog = ev.bool_value;
      } else if (ev.id == "hud_bloom" && ev.type == engine::ui::UiEventType::Toggle) {
        fx.enable_bloom = ev.bool_value;
      }
    }
    retained->set_bool("hud_fog", fx.enable_fog);
    retained->set_bool("hud_bloom", fx.enable_bloom);
    render.set_effect_tuning(fx);

    std::vector<engine::rhi::ScreenQuad> retained_quads;
    for (const auto& r : retained->BuildDrawList()) {
      engine::rhi::ScreenQuad q;
      q.x0 = r.x0;
      q.y0 = r.y0;
      q.x1 = r.x1;
      q.y1 = r.y1;
      q.color = r.color;
      retained_quads.push_back(q);
    }

    // M16 sprites + M7 particle screen proxies
    sprites.clear();
    {
      engine::render2d::Sprite s;
      s.position = {dw - 120.f, 16.f};
      s.size = {96.f, 48.f};
      s.color = {0.2f, 0.75f, 0.95f, 0.85f};
      s.sort_y = s.position.y;
      sprites.push_back(s);
    }
    const float aspect_pre = dh > 0.f ? dw / dh : 1.f;
    const auto vp = app_ref.camera().view_proj_matrix(aspect_pre);
    for (const auto& p : particles.particles()) {
      const auto clip = vp.TransformPoint(p.position);
      // TransformPoint doesn't do perspective divide - use full mat mul
      engine::Vec4 c{p.position.x, p.position.y, p.position.z, 1.f};
      // column-major mul
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
    }
    engine::render2d::SortSprites(sprites);

    const float aspect = dh > 0.f ? dw / dh : 1.f;
    const auto scene = engine::render::RenderSceneExtractor::Extract(
        app_ref.world(), app_ref.camera(), aspect);

    // M20 pick on click when UI not capturing
    if (mouse_pressed && !app_ref.ui_want_capture() && !retained->want_capture()) {
      engine::mixed::PickQuery pq;
      pq.screen_px = {snap.mouse_x, snap.mouse_y};
      pq.viewport_w = dw;
      pq.viewport_h = dh;
      pq.inv_view_proj = scene.camera.view_proj_matrix(aspect).Inverse();
      const auto hit = engine::mixed::Pick(scene.instances, sprites, pq);
      if (hit.kind == engine::mixed::PickHit::Kind::Scene3D) {
        picked_node = hit.node;
      } else {
        picked_node = engine::scene::kInvalidNode;
      }
    }

    profiler.Begin("DrawFrame");
    if (auto st = render.DrawFrame(app_ref.device(), scene, env, aspect, &sprites,
                                   retained_quads.empty() ? nullptr : &retained_quads,
                                   use_vulkan ? nullptr : &dbg);
        !st) {
      engine::LogError(st.message());
    }
    profiler.End("DrawFrame");
    if (imgui_ready) {
      profiler.Begin("ImGui");
      if (auto st = imgui.Render(app_ref.device()); !st) {
        engine::LogError(st.message());
      }
      profiler.End("ImGui");
    }
    app_ref.set_ui_want_capture(app_ref.ui_want_capture() || retained->want_capture());
    profiler.End("Frame");
  });
  return status ? 0 : 1;
}
