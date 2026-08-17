#include "editing/gizmo.h"
#include "editing/selection.h"
#include "editing/undo.h"
#include "editing/voxel_undo.h"
#include "io/scene_io.h"
#include "ui/editor_ui.h"
#include "ui/voxel_ui.h"

#include "game_kit/prefab.h"
#include "game_kit/scene_document.h"
#include "io/world_save.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "world/batch.h"
#include "world/trace.h"

#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/input/input_system.h"
#include "engine/mixed/pick.h"
#include "engine/platform/window.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"
#include "engine/ui/immediate_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace {

void ParseHeadless(int argc, char** argv, engine::ApplicationDesc& desc, bool* voxel) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--voxel" && voxel) {
      *voxel = true;
    }
  }
}

engine::render::RenderSystemDesc LitDesc() {
  const auto dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  engine::render::RenderSystemDesc r{};
  r.lit_vs = dir / "lit_cube.vs.cso";
  r.lit_ps = dir / "lit_cube.ps.cso";
  r.shadow_vs = dir / "shadow.vs.cso";
  r.shadow_ps = dir / "shadow.ps.cso";
  r.quad_vs = dir / "quad.vs.cso";
  r.quad_ps = dir / "quad.ps.cso";
  r.post_vs = dir / "post_ssao_taa.vs.cso";
  r.post_ps = dir / "post_ssao_taa.ps.cso";
  r.debug_vs = dir / "debug_line.vs.cso";
  r.debug_ps = dir / "debug_line.ps.cso";
  r.sky_vs = dir / "skybox.vs.cso";
  r.sky_ps = dir / "skybox.ps.cso";
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  r.quality.enable_ssao = false;
  r.quality.enable_taa = false;
  return r;
}

void SeedScene(engine::scene::World& world) {
  auto ground = world.CreateNode("ground");
  {
    engine::scene::Transform t;
    t.scale = {8.f, 1.f, 8.f};
    world.set_local_transform(ground, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "ground";
    mesh.never_cull = true;
    mesh.local_bounds = {{-4.f, -0.05f, -4.f}, {4.f, 0.05f, 4.f}};
    world.set_mesh(ground, mesh);
  }
  auto cube = world.CreateNode("cube");
  {
    engine::scene::Transform t;
    t.position = {0.f, 0.5f, 0.f};
    world.set_local_transform(cube, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    world.set_mesh(cube, mesh);
  }
}

struct NodeMeta {
  std::string prefab_id;
  std::string script_path;
};

game_kit::PrefabDocument PrefabByIndex(int i) {
  if (i == 1) {
    return game_kit::MakeTreePrefab();
  }
  if (i == 2) {
    return game_kit::MakeHutPrefab();
  }
  return game_kit::MakeChestTagPrefab();
}

void PaintSphere(mc::World& world, editor::VoxelUndo& undo, int x, int y, int z, int r, mc::Id id) {
  const int rad = std::max(r, 0);
  for (int dy = -rad; dy <= rad; ++dy) {
    for (int dz = -rad; dz <= rad; ++dz) {
      for (int dx = -rad; dx <= rad; ++dx) {
        if (dx * dx + dy * dy + dz * dz > rad * rad) {
          continue;
        }
        const int px = x + dx;
        const int py = y + dy;
        const int pz = z + dz;
        const mc::Id before = world.Get(px, py, pz);
        if (before == id) {
          continue;
        }
        world.Set(px, py, pz, id);
        undo.Push(px, py, pz, before, id);
      }
    }
  }
}

void FillBox(mc::World& world, editor::VoxelUndo& undo, int x0, int y0, int z0, int x1, int y1, int z1,
             mc::Id id) {
  const int xa = std::min(x0, x1);
  const int xb = std::max(x0, x1);
  const int ya = std::min(y0, y1);
  const int yb = std::max(y0, y1);
  const int za = std::min(z0, z1);
  const int zb = std::max(z0, z1);
  for (int y = ya; y <= yb; ++y) {
    for (int z = za; z <= zb; ++z) {
      for (int x = xa; x <= xb; ++x) {
        const mc::Id before = world.Get(x, y, z);
        world.Set(x, y, z, id);
        undo.Push(x, y, z, before, id);
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "editor — viewport";
  bool start_voxel = false;
  ParseHeadless(argc, argv, desc, &start_voxel);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }
  auto& a = *app.value();
  a.set_look_with_lmb(false);
  a.set_look_with_rmb(true);
  a.camera().position = {0.f, 2.5f, 6.f};
  a.camera().pitch = -0.25f;
  SeedScene(a.world());

  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  engine::ui::ImmediateUi imgui;
  engine::ui::ImmediateUiDesc ui_desc;
  ui_desc.ui_vs = shader_dir / "ui_imgui.vs.cso";
  ui_desc.ui_ps = shader_dir / "ui_imgui.ps.cso";
  if (auto st = imgui.Init(a.device(), ui_desc); !st) {
    engine::LogWarn("ImmediateUi: " + st.message());
  }

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  editor::Selection sel;
  editor::UndoStack undo;
  editor::VoxelUndo voxel_undo;
  editor::VoxelEdit voxel;
  voxel.enabled = start_voxel;
  mc::GameState vox;
  vox.world.set_seed(1);
  vox.world.StreamAround(0, 0, 3);
  engine::scene::Transform drag_before{};
  bool lmb_prev = false;
  bool rmb_prev = false;
  const auto scene_path = std::filesystem::path("editor_scene.json");
  std::unordered_map<engine::scene::NodeId, NodeMeta> meta;
  int pending_prefab = -1;
  bool playing = false;
  game_kit::SceneDocument scene_snap;
  const auto voxel_snap_dir = std::filesystem::temp_directory_path() / "editor_voxel_play_snap";
  bool paused_play = false;
  bool f3_play = false;
  bool lmb_held_play = false;
  bool rmb_held_play = false;

  const auto status = a.Run([&](engine::Application& app_ref) {
    const auto& snap = app_ref.window().input_snapshot();
    const float w = static_cast<float>(app_ref.window().width());
    const float h = static_cast<float>(app_ref.window().height());
    const float aspect = h > 0.f ? w / h : 1.f;

    imgui.BeginFrame(snap, w, h, app_ref.delta_time());
    bool save = false;
    bool load = false;
    bool vsave = false;
    bool vload = false;
    bool play_scene = false;
    bool play_voxel = false;
    int place_prefab = -1;
    std::string script_edit;
    if (app_ref.world().valid(sel.node) && meta.count(sel.node)) {
      script_edit = meta[sel.node].script_path;
    }
    editor::DrawEditorUi(imgui, app_ref.world(), sel, undo, scene_path, &save, &load, &play_scene,
                         playing && !voxel.enabled, &script_edit, &place_prefab);
    if (app_ref.world().valid(sel.node)) {
      meta[sel.node].script_path = script_edit;
    }
    editor::DrawVoxelUi(imgui, voxel, vox.world, &vsave, &vload, &play_voxel, playing && voxel.enabled);
    imgui.RefreshCapture();
    app_ref.set_ui_want_capture(imgui.want_capture_mouse() || imgui.want_capture_keyboard());

    if (place_prefab >= 0) {
      pending_prefab = place_prefab;
    }

    if (save) {
      auto doc = game_kit::CaptureWorld(app_ref.world());
      for (auto& n : doc.nodes) {
        const auto id = static_cast<engine::scene::NodeId>(std::strtoul(n.id.c_str(), nullptr, 10));
        auto it = meta.find(id);
        if (it != meta.end()) {
          n.prefab_id = it->second.prefab_id;
          n.script_path = it->second.script_path;
        }
      }
      if (auto st = game_kit::SaveSceneDocument(doc, scene_path); !st) {
        engine::LogError(st.message());
      } else {
        engine::LogInfo("saved " + scene_path.string());
      }
    }
    if (load) {
      auto doc = game_kit::LoadSceneDocument(scene_path);
      if (!doc) {
        engine::LogError(doc.status().message());
      } else {
        game_kit::ClearWorld(app_ref.world());
        if (auto st = game_kit::ApplyWorld(app_ref.world(), doc.value()); !st) {
          engine::LogError(st.message());
        } else {
          sel = {};
          undo.Clear();
          meta.clear();
          auto cap = game_kit::CaptureWorld(app_ref.world());
          for (std::size_t i = 0; i < doc.value().nodes.size() && i < cap.nodes.size(); ++i) {
            const auto id =
                static_cast<engine::scene::NodeId>(std::strtoul(cap.nodes[i].id.c_str(), nullptr, 10));
            meta[id] = NodeMeta{doc.value().nodes[i].prefab_id, doc.value().nodes[i].script_path};
          }
          engine::LogInfo("loaded " + scene_path.string());
        }
      }
    }
    if (vsave) {
      if (auto st = mc::SaveWorld(vox, voxel.dir); !st) {
        engine::LogError(st.message());
      } else {
        voxel.dirty = false;
        engine::LogInfo("voxel saved");
      }
    }
    if (vload) {
      if (auto st = mc::LoadWorld(&vox, voxel.dir); !st) {
        engine::LogError(st.message());
      } else {
        voxel_undo.Clear();
        voxel.dirty = false;
        engine::LogInfo("voxel loaded");
      }
    }

    auto toggle_play = [&](bool voxel_play) {
      if (!playing) {
        playing = true;
        if (voxel_play) {
          voxel.enabled = true;
          (void)mc::SaveWorld(vox, voxel_snap_dir);
          vox.player.pos = app_ref.camera().position - engine::Vec3{0.f, 1.62f, 0.f};
          vox.player.creative = true;
          vox.player.flying = true;
          mc::FillCreativeInventory(&vox.player.inv);
          app_ref.set_move_speed(0.f);
        } else {
          scene_snap = game_kit::CaptureWorld(app_ref.world());
        }
      } else {
        playing = false;
        if (voxel_play || voxel.enabled) {
          (void)mc::LoadWorld(&vox, voxel_snap_dir);
          app_ref.set_move_speed(5.5f);
        } else {
          game_kit::ClearWorld(app_ref.world());
          (void)game_kit::ApplyWorld(app_ref.world(), scene_snap);
        }
      }
    };
    if (play_scene && !voxel.enabled) {
      toggle_play(false);
    }
    if (play_voxel) {
      toggle_play(true);
    }

    const bool lmb = snap.mouse_left && !app_ref.ui_want_capture();
    const bool rmb = snap.mouse_right && !app_ref.ui_want_capture();
    const bool pressed = lmb && !lmb_prev;
    const bool held = lmb && lmb_prev;
    const bool z_down = snap.keys[0x5A] != 0;
    static bool z_prev = false;
    if (!playing && z_down && !z_prev && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
      if (voxel.enabled) {
        (void)voxel_undo.Undo(vox.world);
      } else {
        (void)undo.Undo(app_ref.world());
      }
    }
    z_prev = z_down;

    if (playing && voxel.enabled) {
      mc::GameInput in;
      in.snap = &snap;
      in.lmb = snap.mouse_left;
      in.rmb = snap.mouse_right;
      in.mmb = snap.mouse_middle;
      in.lmb_pressed = snap.mouse_left && !lmb_held_play;
      in.rmb_pressed = snap.mouse_right && !rmb_held_play;
      lmb_held_play = snap.mouse_left;
      rmb_held_play = snap.mouse_right;
      if (app_ref.input().key_down(engine::input::Key::W)) {
        in.wish.z += 1.f;
      }
      if (app_ref.input().key_down(engine::input::Key::S)) {
        in.wish.z -= 1.f;
      }
      if (app_ref.input().key_down(engine::input::Key::A)) {
        in.wish.x -= 1.f;
      }
      if (app_ref.input().key_down(engine::input::Key::D)) {
        in.wish.x += 1.f;
      }
      in.jump = app_ref.input().key_down(engine::input::Key::Space);
      in.sneak = snap.keys[VK_SHIFT] != 0;
      if (vox.player.flying && snap.keys[VK_SHIFT]) {
        in.wish.y -= 1.f;
      }
      if (vox.player.flying && in.jump) {
        in.wish.y += 1.f;
      }
      std::vector<mc::SlotHit> hits;
      (void)mc::TickGameplay(&vox, app_ref.camera(), in, app_ref.delta_time(), &paused_play, &f3_play,
                             hits);
      mc::SyncCamera(vox.player, &app_ref.camera());
    } else if (!playing && voxel.enabled) {
      const int cx = static_cast<int>(app_ref.camera().position.x);
      const int cz = static_cast<int>(app_ref.camera().position.z);
      vox.world.StreamAround(cx, cz, 4);
      const auto hit =
          mc::TraceBlocks(vox.world, app_ref.camera().position, mc::LookDir(app_ref.camera()), 12.f);
      const bool erase = snap.keys[0x58] != 0 || (snap.keys[VK_SHIFT] && lmb);
      if (pressed && hit.hit) {
        if (voxel.box_mode) {
          if (!voxel.box_has_a) {
            voxel.ax = hit.x;
            voxel.ay = hit.y;
            voxel.az = hit.z;
            voxel.box_has_a = true;
          } else {
            FillBox(vox.world, voxel_undo, voxel.ax, voxel.ay, voxel.az, hit.x, hit.y, hit.z,
                    erase ? mc::Id::Air : voxel.brush);
            voxel.box_has_a = false;
            voxel.dirty = true;
          }
        } else {
          PaintSphere(vox.world, voxel_undo, hit.x, hit.y, hit.z, voxel.brush_radius,
                      erase ? mc::Id::Air : voxel.brush);
          voxel.dirty = true;
        }
      }
      if (snap.keys[0x58] && hit.hit && !pressed) {
        PaintSphere(vox.world, voxel_undo, hit.x, hit.y, hit.z, voxel.brush_radius, mc::Id::Air);
        voxel.dirty = true;
      }
    } else if (!playing && pressed) {
      if (pending_prefab >= 0) {
        engine::scene::Transform t;
        const engine::Vec3 dir = mc::LookDir(app_ref.camera());
        t.position = app_ref.camera().position + dir * 4.f;
        t.position.y = std::max(t.position.y, 0.f);
        const auto prefab = PrefabByIndex(pending_prefab);
        const auto id = game_kit::Instantiate(app_ref.world(), prefab, t);
        if (app_ref.world().valid(id)) {
          sel.node = id;
          meta[id] = NodeMeta{prefab.prefab_id, prefab.scene.nodes.empty() ? "" : prefab.scene.nodes[0].script_path};
        }
        pending_prefab = -1;
      } else {
        engine::mixed::PickQuery q;
        q.screen_px = {snap.mouse_x, snap.mouse_y};
        q.viewport_w = w;
        q.viewport_h = h;
        q.inv_view_proj = app_ref.camera().view_proj_matrix(aspect).Inverse();
        const auto hit = engine::mixed::Pick(app_ref.render_scene().instances, {}, q);
        if (hit.kind == engine::mixed::PickHit::Kind::Scene3D) {
          sel.node = hit.node;
          if (app_ref.world().valid(sel.node)) {
            drag_before = app_ref.world().local_transform(sel.node);
          }
        }
      }
    } else if (!playing && held && app_ref.world().valid(sel.node)) {
      engine::scene::Transform now{};
      if (editor::TranslateXz(app_ref.world(), sel.node, snap.mouse_dx, snap.mouse_dy, 0.01f, &now)) {
        sel.dragging = true;
      }
    } else if (!playing && !lmb && lmb_prev && sel.dragging && app_ref.world().valid(sel.node)) {
      undo.Push(sel.node, drag_before, app_ref.world().local_transform(sel.node));
      sel.dragging = false;
    }
    lmb_prev = lmb;
    rmb_prev = rmb;

    app_ref.debug_draw().AddGrid(8.f, 1.f, 0.f);
    if (!voxel.enabled && !playing) {
      editor::DrawGizmo(app_ref.debug_draw(), app_ref.world(), sel.node);
    }

    if (voxel.enabled) {
      std::vector<engine::rhi::LitDrawItem> opaque;
      std::vector<engine::rhi::LitDrawItem> water;
      mc::CollectVisible(vox.world, app_ref.camera().position, 3, &opaque, &water);
      mc::LitSubmit sub;
      mc::PrepareLitSubmit(opaque, app_ref.camera().position, &sub);
      mc::QueueWorldDraws(app_ref.device(), render, sub, water);
    }
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect, nullptr,
                                   nullptr, &app_ref.debug_draw());
        !st) {
      engine::LogError(st.message());
    }
    if (voxel.enabled) {
      engine::rhi::ScreenQuad preview;
      preview.x0 = w - 48.f;
      preview.y0 = 12.f;
      preview.x1 = w - 12.f;
      preview.y1 = 48.f;
      preview.color = mc::GetDef(voxel.brush).color;
      preview.color.a = 1.f;
      (void)app_ref.device().DrawScreenQuads(std::span<const engine::rhi::ScreenQuad>(&preview, 1));
    }
    (void)imgui.Render(app_ref.device());
  });
  return status ? 0 : 1;
}
