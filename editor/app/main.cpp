#include "editing/gizmo.h"
#include "editing/ops.h"
#include "editing/ray.h"
#include "editing/selection.h"
#include "editing/settings.h"
#include "editing/snap.h"
#include "editing/undo.h"
#include "editing/voxel_undo.h"
#include "io/content_browser.h"
#include "io/scene_io.h"
#include "play/scene_play.h"
#include "ui/editor_ui.h"
#include "ui/voxel_ui.h"

#include "game_kit/prefab.h"
#include "game_kit/runtime.h"
#include "game_kit/scene_document.h"
#include "game_kit/script_component.h"
#include "io/world_save.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/blocks.h"
#include "ui/hud.h"
#include "world/batch.h"
#include "world/chunk.h"
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
#include "engine/ui/rml_ui.h"
#include "engine/ui/retained_ui.h"

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

using NodeMeta = editor::NodeMeta;

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
  undo.BeginGroup();
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
  undo.EndGroup();
}

void FillBox(mc::World& world, editor::VoxelUndo& undo, int x0, int y0, int z0, int x1, int y1, int z1,
             mc::Id id) {
  const int xa = std::min(x0, x1);
  const int xb = std::max(x0, x1);
  const int ya = std::min(y0, y1);
  const int yb = std::max(y0, y1);
  const int za = std::min(z0, z1);
  const int zb = std::max(z0, z1);
  undo.BeginGroup();
  for (int y = ya; y <= yb; ++y) {
    for (int z = za; z <= zb; ++z) {
      for (int x = xa; x <= xb; ++x) {
        const mc::Id before = world.Get(x, y, z);
        world.Set(x, y, z, id);
        undo.Push(x, y, z, before, id);
      }
    }
  }
  undo.EndGroup();
}

void FillLayer(mc::World& world, editor::VoxelUndo& undo, int y, mc::Id id) {
  if (y < 0 || y >= mc::kChunkH) {
    return;
  }
  undo.BeginGroup();
  for (auto& kv : world.chunks()) {
    const int ox = kv.first.x * mc::kChunkW;
    const int oz = kv.first.z * mc::kChunkW;
    for (int lz = 0; lz < mc::kChunkW; ++lz) {
      for (int lx = 0; lx < mc::kChunkW; ++lx) {
        const int px = ox + lx;
        const int pz = oz + lz;
        const mc::Id before = world.Get(px, y, pz);
        if (before == id) {
          continue;
        }
        world.Set(px, y, pz, id);
        undo.Push(px, y, pz, before, id);
      }
    }
  }
  undo.EndGroup();
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
  editor::EditorSettings settings;
  editor::VoxelEdit voxel;
  voxel.enabled = start_voxel;
  mc::GameState vox;
  vox.world.set_seed(1);
  vox.world.StreamAround(0, 0, 3);
  bool lmb_prev = false;
  bool rmb_prev = false;
  const auto scene_path = std::filesystem::path("editor_scene.json");
  std::unordered_map<engine::scene::NodeId, NodeMeta> meta;
  int pending_prefab = -1;
  int pending_content = -1;
  bool playing = false;
  game_kit::SceneDocument scene_snap;
  const auto voxel_snap_dir = std::filesystem::temp_directory_path() / "editor_voxel_play_snap";
  bool paused_play = false;
  bool f3_play = false;
  bool lmb_held_play = false;
  bool rmb_held_play = false;
  std::vector<mc::SlotHit> voxel_hud_hits;
  game_kit::GameRuntime rt;
  game_kit::ScriptComponentWorld play_scripts;
  editor::ContentBrowser content;
  std::vector<std::string> script_files;
  auto rescan_content = [&] {
    content.Scan({std::filesystem::path("editor/content"), std::filesystem::path("game_kit/samples")});
    editor::ScanLuaScripts({std::filesystem::path("scripts"), std::filesystem::path("editor/scripts")},
                           &script_files);
  };
  rescan_content();
  auto hud = engine::ui::CreateRetainedUiBackend();

  const auto status = a.Run([&](engine::Application& app_ref) {
    const auto& snap = app_ref.window().input_snapshot();
    const float w = static_cast<float>(app_ref.window().width());
    const float h = static_cast<float>(app_ref.window().height());
    const float aspect = h > 0.f ? w / h : 1.f;

    imgui.BeginFrame(snap, w, h, app_ref.delta_time());
    editor::EditorCommands cmd;
    editor::VoxelCommands vcmd;
    std::string script_edit;
    if (app_ref.world().valid(sel.node) && meta.count(sel.node)) {
      script_edit = meta[sel.node].script_path;
    }
    editor::DrawEditorUi(imgui, app_ref.world(), sel, undo, scene_path, settings, &cmd, &script_edit,
                         playing && !voxel.enabled, paused_play, &content, script_files,
                         app_ref.delta_time() > 1e-4f ? 1.f / app_ref.delta_time() : 0.f);
    if (app_ref.world().valid(sel.node)) {
      meta[sel.node].script_path = script_edit;
    }
    editor::DrawVoxelUi(imgui, voxel, vox.world, &vcmd, playing && voxel.enabled);
    imgui.RefreshCapture();
    app_ref.set_ui_want_capture(imgui.want_capture_mouse() || imgui.want_capture_keyboard());

    if (cmd.place_prefab >= 0) {
      pending_prefab = cmd.place_prefab;
      pending_content = -1;
    }

    if (cmd.rescan) {
      rescan_content();
    }
    if (cmd.cook) {
      (void)editor::TryRunAssetCook();
    }
    if (cmd.save_prefab && app_ref.world().valid(sel.node) && !playing) {
      const auto name = app_ref.world().name(sel.node).empty() ? "selection" : app_ref.world().name(sel.node);
      const auto path = std::filesystem::path("editor/content") / (name + ".json");
      if (auto st = editor::SaveSelectionPrefab(app_ref.world(), sel.node, path); !st) {
        engine::LogError(st.message());
      } else {
        engine::LogInfo("prefab saved " + path.string());
        rescan_content();
      }
    }
    if (cmd.place_content >= 0) {
      pending_content = cmd.place_content;
      pending_prefab = -1;
    }

    auto persist_scene = [&] {
      auto doc = game_kit::CaptureWorld(app_ref.world());
      editor::StampMeta(&doc, meta);
      if (auto st = game_kit::SaveSceneDocument(doc, scene_path); !st) {
        engine::LogError(st.message());
      } else {
        settings.dirty = false;
        engine::LogInfo("saved " + scene_path.string());
      }
    };
    if (cmd.save) {
      persist_scene();
    }
    if (cmd.load) {
      auto doc = game_kit::LoadSceneDocument(scene_path);
      if (!doc) {
        engine::LogError(doc.status().message());
      } else {
        game_kit::ClearWorld(app_ref.world());
        if (auto st = game_kit::ApplyWorld(app_ref.world(), doc.value()); !st) {
          engine::LogError(st.message());
        } else {
          sel.Clear();
          undo.Clear();
          settings.dirty = false;
          auto cap = game_kit::CaptureWorld(app_ref.world());
          editor::RestoreMeta(doc.value(), cap, &meta);
          engine::LogInfo("loaded " + scene_path.string());
        }
      }
    }
    if (vcmd.save) {
      if (auto st = mc::SaveWorld(vox, voxel.dir); !st) {
        engine::LogError(st.message());
      } else {
        voxel.dirty = false;
        engine::LogInfo("voxel saved");
      }
    }
    if (vcmd.load) {
      if (auto st = mc::LoadWorld(&vox, voxel.dir); !st) {
        engine::LogError(st.message());
      } else {
        voxel_undo.Clear();
        voxel.dirty = false;
        engine::LogInfo("voxel loaded");
      }
    }
    if (vcmd.fill_layer && !playing) {
      FillLayer(vox.world, voxel_undo, voxel.layer_y, voxel.brush);
      voxel.dirty = true;
    }

    auto toggle_play = [&](bool voxel_play) {
      if (!playing) {
        playing = true;
        paused_play = false;
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
          editor::StampMeta(&scene_snap, meta);
          editor::BindPlayScripts(play_scripts, app_ref.world(), rt, meta);
          if (app_ref.world().valid(editor::FindNamed(app_ref.world(), "player"))) {
            app_ref.set_move_speed(0.f);
          }
        }
      } else {
        playing = false;
        paused_play = false;
        play_scripts.Clear();
        if (voxel_play || voxel.enabled) {
          (void)mc::LoadWorld(&vox, voxel_snap_dir);
          app_ref.set_move_speed(5.5f);
        } else {
          game_kit::ClearWorld(app_ref.world());
          (void)game_kit::ApplyWorld(app_ref.world(), scene_snap);
          auto cap = game_kit::CaptureWorld(app_ref.world());
          editor::RestoreMeta(scene_snap, cap, &meta);
          app_ref.set_move_speed(5.5f);
        }
      }
    };
    if (cmd.play && !voxel.enabled) {
      toggle_play(false);
    }
    if (vcmd.play) {
      toggle_play(true);
    }
    if (cmd.pause || vcmd.pause) {
      paused_play = !paused_play;
    }

    const bool lmb = snap.mouse_left && !app_ref.ui_want_capture();
    const bool rmb = snap.mouse_right && !app_ref.ui_want_capture();
    const bool pressed = lmb && !lmb_prev;
    const bool held = lmb && lmb_prev;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool z_down = snap.keys[0x5A] != 0;
    const bool y_down = snap.keys[0x59] != 0;
    const bool d_down = snap.keys[0x44] != 0;
    const bool s_down = snap.keys[0x53] != 0;
    const bool f_down = snap.keys[0x46] != 0;
    const bool del_down = snap.keys[VK_DELETE] != 0;
    static bool z_prev = false;
    static bool y_prev = false;
    static bool d_prev = false;
    static bool s_prev = false;
    static bool f_prev = false;
    static bool del_prev = false;
    if (!playing) {
      if (cmd.undo || (z_down && !z_prev && ctrl)) {
        if (voxel.enabled) {
          (void)voxel_undo.Undo(vox.world);
        } else {
          (void)undo.Undo(app_ref.world());
        }
      }
      if (cmd.redo || (y_down && !y_prev && ctrl)) {
        if (voxel.enabled) {
          (void)voxel_undo.Redo(vox.world);
        } else {
          (void)undo.Redo(app_ref.world());
        }
      }
      if (s_down && !s_prev && ctrl) {
        persist_scene();
      }
      if ((cmd.duplicate || (d_down && !d_prev && ctrl)) && !voxel.enabled) {
        const float off = settings.snap ? settings.grid : 1.f;
        const auto created = editor::DuplicateSelection(app_ref.world(), sel, off);
        if (!created.empty()) {
          sel.Set(created.front());
          for (std::size_t i = 1; i < created.size(); ++i) {
            sel.Toggle(created[i]);
          }
          settings.dirty = true;
        }
      }
      if ((cmd.destroy || (del_down && !del_prev)) && !voxel.enabled) {
        editor::DestroySelection(app_ref.world(), &sel);
        settings.dirty = true;
      }
      if ((cmd.frame || (f_down && !f_prev)) && !voxel.enabled) {
        editor::FrameCamera(&app_ref.camera(), app_ref.world(), sel.node);
      }
    }
    z_prev = z_down;
    y_prev = y_down;
    d_prev = d_down;
    s_prev = s_down;
    f_prev = f_down;
    del_prev = del_down;

    const bool k1 = snap.keys[0x31] != 0;
    const bool k2 = snap.keys[0x32] != 0;
    const bool k3 = snap.keys[0x33] != 0;
    static bool k1_prev = false;
    static bool k2_prev = false;
    static bool k3_prev = false;
    if (!playing && !voxel.enabled && !app_ref.ui_want_capture()) {
      if (k1 && !k1_prev) {
        settings.gizmo_mode = 0;
      }
      if (k2 && !k2_prev) {
        settings.gizmo_mode = 1;
      }
      if (k3 && !k3_prev) {
        settings.gizmo_mode = 2;
      }
    }
    k1_prev = k1;
    k2_prev = k2;
    k3_prev = k3;

    auto store_origins = [&] {
      sel.drag_origins.clear();
      for (auto id : sel.All()) {
        if (app_ref.world().valid(id)) {
          sel.drag_origins.push_back(app_ref.world().local_transform(id));
        }
      }
    };
    auto place_at_cursor = [&](const game_kit::PrefabDocument& prefab) {
      engine::scene::Transform t;
      const engine::Vec3 dir = mc::LookDir(app_ref.camera());
      t.position = app_ref.camera().position + dir * 4.f;
      t.position.y = std::max(t.position.y, 0.f);
      if (settings.snap) {
        editor::SnapTransform(&t, settings.grid);
      }
      const auto id = game_kit::Instantiate(app_ref.world(), prefab, t);
      if (app_ref.world().valid(id)) {
        sel.Set(id);
        meta[id] = NodeMeta{prefab.prefab_id,
                            prefab.scene.nodes.empty() ? "" : prefab.scene.nodes[0].script_path};
        settings.dirty = true;
      }
    };

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
      (void)mc::TickGameplay(&vox, app_ref.camera(), in, app_ref.delta_time(), &paused_play, &f3_play,
                             voxel_hud_hits);
      mc::SyncCamera(vox.player, &app_ref.camera());
    } else if (playing && !voxel.enabled) {
      rt.set_paused(paused_play);
      if (!paused_play) {
        rt.Tick(app_ref, app_ref.delta_time());
        play_scripts.Tick(app_ref.delta_time());
        const auto player = editor::FindNamed(app_ref.world(), "player");
        if (app_ref.world().valid(player)) {
          engine::Vec3 wish{};
          if (app_ref.input().key_down(engine::input::Key::W)) {
            wish.z += 1.f;
          }
          if (app_ref.input().key_down(engine::input::Key::S)) {
            wish.z -= 1.f;
          }
          if (app_ref.input().key_down(engine::input::Key::A)) {
            wish.x -= 1.f;
          }
          if (app_ref.input().key_down(engine::input::Key::D)) {
            wish.x += 1.f;
          }
          editor::MovePlayerOnGround(app_ref.world(), player, app_ref.camera().yaw, wish, 5.5f,
                                    app_ref.delta_time());
          editor::FollowPlayerCamera(&app_ref.camera(), app_ref.world().local_transform(player).position);
        }
      }
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
      bool handled = false;
      if (pending_prefab >= 0) {
        place_at_cursor(PrefabByIndex(pending_prefab));
        pending_prefab = -1;
        handled = true;
      } else if (pending_content >= 0 && pending_content < static_cast<int>(content.items.size())) {
        const auto& item = content.items[static_cast<std::size_t>(pending_content)];
        if (item.kind == editor::ContentItem::Kind::Prefab) {
          auto prefab = game_kit::LoadPrefabDocument(item.path);
          if (prefab) {
            place_at_cursor(prefab.value());
          } else {
            engine::LogError(prefab.status().message());
          }
        } else {
          auto doc = game_kit::LoadSceneDocument(item.path);
          if (doc) {
            if (auto st = game_kit::ApplyWorld(app_ref.world(), doc.value()); !st) {
              engine::LogError(st.message());
            } else {
              settings.dirty = true;
            }
          } else {
            engine::LogError(doc.status().message());
          }
        }
        pending_content = -1;
        handled = true;
      } else if (settings.show_gizmo && app_ref.world().valid(sel.node)) {
        const auto inv_vp = app_ref.camera().view_proj_matrix(aspect).Inverse();
        const auto ray = editor::ScreenRay(snap.mouse_x, snap.mouse_y, w, h, inv_vp);
        const auto origin = app_ref.world().local_transform(sel.node).position;
        const auto hit_ax =
            editor::HitGizmoAxes(ray, origin, editor::kGizmoLength, editor::kGizmoHitRadius);
        if (hit_ax != editor::Axis::None) {
          sel.gizmo_axis = static_cast<int>(hit_ax);
          sel.axis_u0 = editor::AxisParam(ray, origin, hit_ax);
          sel.drag_acc_x = 0.f;
          sel.drag_acc_z = 0.f;
          store_origins();
          handled = true;
        }
      }
      if (!handled) {
        engine::mixed::PickQuery q;
        q.screen_px = {snap.mouse_x, snap.mouse_y};
        q.viewport_w = w;
        q.viewport_h = h;
        q.inv_view_proj = app_ref.camera().view_proj_matrix(aspect).Inverse();
        const auto hit = engine::mixed::Pick(app_ref.render_scene().instances, {}, q);
        if (hit.kind == engine::mixed::PickHit::Kind::Scene3D) {
          if (ctrl) {
            sel.Toggle(hit.node);
          } else {
            sel.Set(hit.node);
          }
          sel.gizmo_axis = 0;
          sel.drag_acc_x = 0.f;
          sel.drag_acc_z = 0.f;
          store_origins();
        }
      }
    } else if (!playing && held && app_ref.world().valid(sel.node)) {
      const auto ids = sel.All();
      const auto mode = static_cast<editor::GizmoMode>(settings.gizmo_mode);
      if (sel.gizmo_axis != 0) {
        float delta = 0.f;
        if (mode == editor::GizmoMode::Rotate) {
          sel.drag_acc_x += snap.mouse_dx;
          delta = sel.drag_acc_x * 0.012f;
        } else if (!sel.drag_origins.empty()) {
          const auto inv_vp = app_ref.camera().view_proj_matrix(aspect).Inverse();
          const auto ray = editor::ScreenRay(snap.mouse_x, snap.mouse_y, w, h, inv_vp);
          delta = editor::AxisParam(ray, sel.drag_origins.front().position,
                                    static_cast<editor::Axis>(sel.gizmo_axis)) -
                  sel.axis_u0;
        }
        if (editor::ApplyGizmo(app_ref.world(), ids, sel.drag_origins, mode,
                               static_cast<editor::Axis>(sel.gizmo_axis), delta, settings.snap,
                               settings.grid)) {
          sel.dragging = true;
          settings.dirty = true;
        }
      } else {
        sel.drag_acc_x += snap.mouse_dx;
        sel.drag_acc_z += snap.mouse_dy;
        if (editor::TranslateSelection(app_ref.world(), ids, sel.drag_origins, sel.drag_acc_x,
                                       sel.drag_acc_z, 0.01f, settings.snap, settings.grid)) {
          sel.dragging = true;
          settings.dirty = true;
        }
      }
    } else if (!playing && !lmb && lmb_prev && sel.dragging) {
      std::vector<engine::scene::Transform> after;
      const auto ids = sel.All();
      after.reserve(ids.size());
      for (auto id : ids) {
        if (app_ref.world().valid(id)) {
          after.push_back(app_ref.world().local_transform(id));
        }
      }
      if (!sel.drag_origins.empty() && after.size() == sel.drag_origins.size()) {
        undo.PushBatch(ids, sel.drag_origins, after);
      }
      sel.dragging = false;
      sel.gizmo_axis = 0;
      sel.drag_acc_x = 0.f;
      sel.drag_acc_z = 0.f;
    }
    lmb_prev = lmb;
    rmb_prev = rmb;

    if (settings.show_grid) {
      app_ref.debug_draw().AddGrid(8.f, settings.snap ? settings.grid : 1.f, 0.f);
    }
    if (!voxel.enabled && !playing && settings.show_gizmo) {
      const auto ids = sel.All();
      editor::DrawGizmos(app_ref.debug_draw(), app_ref.world(), ids,
                         static_cast<editor::GizmoMode>(settings.gizmo_mode));
    }

    if (voxel.enabled) {
      std::vector<engine::rhi::LitDrawItem> opaque;
      std::vector<engine::rhi::LitDrawItem> water;
      mc::CollectVisible(vox.world, app_ref.camera().position, 3, &opaque, &water);
      mc::LitSubmit sub;
      mc::PrepareLitSubmit(opaque, app_ref.camera().position, &sub);
      mc::QueueWorldDraws(app_ref.device(), render, sub, water);
    }
    std::vector<engine::rhi::ScreenQuad> ui_quads;
    if (voxel.enabled && playing && hud) {
      mc::HudParams hp;
      hp.paused = paused_play;
      hp.in_menu = false;
      hp.f3 = f3_play;
      hp.yaw = app_ref.camera().yaw;
      hp.pitch = app_ref.camera().pitch;
      hp.view_radius = vox.view_radius;
      hp.mouse_x = snap.mouse_x;
      hp.mouse_y = snap.mouse_y;
      hp.look = mc::TraceBlocks(vox.world, mc::Eye(vox.player), mc::LookDir(app_ref.camera()), 6.f);
      if (hp.look.hit) {
        hp.look_id = vox.world.Get(hp.look.x, hp.look.y, hp.look.z);
        hp.break_need = mc::BreakTime(hp.look_id, vox.player.inv.Hotbar().id);
      }
      std::vector<mc::SlotHit> hud_hits;
      mc::BuildHud(*hud, vox.player, &vox.boxes, static_cast<int>(w), static_cast<int>(h), hp,
                   &ui_quads, &hud_hits);
      voxel_hud_hits = std::move(hud_hits);
    }
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect, nullptr,
                                   ui_quads.empty() ? nullptr : &ui_quads, &app_ref.debug_draw());
        !st) {
      engine::LogError(st.message());
    }
    if (voxel.enabled && !playing) {
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
