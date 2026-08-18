#include "editing/anim_edit.h"
#include "editing/gizmo.h"
#include "editing/ops.h"
#include "editing/ray.h"
#include "editing/selection.h"
#include "editing/settings.h"
#include "editing/snap.h"
#include "editing/terrain_edit.h"
#include "editing/tile_edit.h"
#include "editing/undo.h"
#include "editing/viewport_layout.h"
#include "editing/voxel_undo.h"
#include "cmd/session.h"
#include "mcp/live_io.h"
#include "mcp/protocol.h"
#include "io/content_browser.h"
#include "io/dep_graph.h"
#include "io/scene_ext.h"
#include "play/scene_play.h"
#include "ui/editor_ui.h"
#include "ui/voxel_ui.h"

#include "game_kit/prefab.h"
#include "game_kit/runtime.h"
#include "game_kit/scene_document.h"
#include "game_kit/script_component.h"
#include "game_kit/script_hot_reload.h"
#include "io/world_save.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/blocks.h"
#include "ui/hud.h"
#include "world/batch.h"
#include "world/chunk.h"
#include "world/trace.h"

#include "engine/assets/asset_hot_reload.h"
#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/debug/console.h"
#include "engine/debug/debug_draw.h"
#include "engine/input/input_system.h"
#include "engine/mixed/pick.h"
#include "engine/physics/i_physics_world.h"
#include "engine/platform/window.h"
#include "engine/render/environment.h"
#include "engine/render/local_lights.h"
#include "engine/render/quality.h"
#include "engine/render/render_scene.h"
#include "engine/render/render_system.h"
#include "engine/render2d/tilemap_stream.h"
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
  engine::debug::Profiler profiler;
  render.SetProfiler(&profiler);

  editor::Selection sel;
  editor::UndoStack undo;
  editor::VoxelUndo voxel_undo;
  editor::EditorSettings settings;
  settings.heights.assign(17 * 17, 0.f);
  settings.tiles.assign(16 * 16, 0);
  editor::LiveServer live;
  (void)live.Start();
  game_kit::ScriptHotReload hot;
  hot.SetRoot(std::filesystem::path("editor/scripts"));
  std::unique_ptr<engine::physics::IPhysicsWorld> phys;
  engine::render::Camera edit_cam = a.camera();
  editor::VoxelEdit voxel;
  voxel.enabled = start_voxel;
  settings.workspace = start_voxel ? 1 : 0;
  mc::GameState vox;
  vox.world.set_seed(1);
  vox.world.StreamAround(0, 0, 3);
  bool lmb_prev = false;
  bool rmb_prev = false;
  auto scene_path = std::filesystem::path("editor_scene.json");
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
  engine::scene::World play_world;
  engine::render2d::TilemapStreamer tiles;
  engine::assets::AssetHotReload assets_hot;
  assets_hot.SetRoot(std::filesystem::path("editor/content"));
  editor::SyncStreamer(settings.tiles, &tiles);
  std::string lint_json;
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

    engine::scene::World& live_world =
        (playing && !voxel.enabled) ? play_world : app_ref.world();
    imgui.BeginFrame(snap, w, h, app_ref.delta_time());
    const bool ctrl_ui = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    editor::EditorCommands cmd;
    editor::VoxelCommands vcmd;
    std::string script_edit;
    if (live_world.valid(sel.node) && meta.count(sel.node)) {
      script_edit = meta[sel.node].script_path;
    }
    editor::DrawEditorUi(imgui, live_world, sel, undo, scene_path, settings, &cmd, &script_edit,
                         playing && !voxel.enabled, paused_play, &content, script_files,
                         app_ref.delta_time() > 1e-4f ? 1.f / app_ref.delta_time() : 0.f, ctrl_ui,
                         &meta);
    if (live_world.valid(sel.node)) {
      meta[sel.node].script_path = script_edit;
    }
    if (cmd.anim_state >= 0 && live_world.valid(sel.node)) {
      auto h0 = settings.heights;
      auto t0 = settings.tiles;
      auto a0 = settings.anim;
      settings.anim.current = cmd.anim_state;
      meta[sel.node].anim_state = editor::CurrentState(settings.anim);
      const std::string nm =
          live_world.name(sel.node).empty() ? "node" : live_world.name(sel.node);
      rt.anims().GetOrCreate(nm).Play(meta[sel.node].anim_state, true);
      undo.PushGrid(std::move(h0), std::move(t0), std::move(a0), settings.heights, settings.tiles,
                    settings.anim);
      settings.dirty = true;
    }
    voxel.enabled = settings.workspace == 1;
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

    auto apply_scene_file = [&](const std::filesystem::path& path) {
      auto doc = game_kit::LoadSceneDocument(path);
      if (!doc) {
        engine::LogError(doc.status().message());
        return false;
      }
      game_kit::ClearWorld(app_ref.world());
      if (auto st = game_kit::ApplyWorld(app_ref.world(), doc.value()); !st) {
        engine::LogError(st.message());
        return false;
      }
      sel.Clear();
      undo.Clear();
      settings.dirty = false;
      scene_path = path;
      auto cap = game_kit::CaptureWorld(app_ref.world());
      editor::RestoreMeta(doc.value(), cap, &meta);
      editor::SyncWorldToMeta(app_ref.world(), &meta);
      editor::UnpackEditorExtensions(doc.value(), &settings);
      editor::SyncStreamer(settings.tiles, &tiles);
      engine::LogInfo("loaded " + path.string());
      return true;
    };
    if (cmd.open_scene >= 0 && !playing &&
        cmd.open_scene < static_cast<int>(content.items.size())) {
      const auto& item = content.items[static_cast<std::size_t>(cmd.open_scene)];
      (void)apply_scene_file(item.path);
    }

    auto persist_scene = [&] {
      auto doc = game_kit::CaptureWorld(app_ref.world());
      editor::StampMeta(&doc, meta);
      editor::PackEditorExtensions(settings, &doc);
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
      (void)apply_scene_file(scene_path);
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

    auto bind_session = [&]() {
      editor::EditorSession s;
      s.edit_world = &app_ref.world();
      s.play_world = &play_world;
      s.world = (playing && !voxel.enabled) ? &play_world : &app_ref.world();
      s.sel = &sel;
      s.undo = &undo;
      s.settings = &settings;
      s.meta = &meta;
      s.scene_path = &scene_path;
      s.playing = &playing;
      s.paused = &paused_play;
      s.rt = &rt;
      s.scripts = &play_scripts;
      s.scene_snap = &scene_snap;
      s.content = &content;
      s.lint_json = &lint_json;
      s.device = &app_ref.device();
      return s;
    };

    {
      std::vector<std::string> live_lines;
      live.Poll(&live_lines);
      for (const auto& line : live_lines) {
        const auto out = editor::HandleMcpSession(bind_session(), line);
        if (!out.empty()) {
          live.Reply(out);
        }
      }
    }

    if (cmd.create_kind >= 0 && !playing && !voxel.enabled) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::Create;
      const char* kinds[] = {"cube", "empty", "ground", "player", "light", "camera", "collider", "sprite"};
      op.create_kind = kinds[cmd.create_kind < 8 ? cmd.create_kind : 0];
      (void)editor::ApplyOp(bind_session(), op);
    }
    if (!cmd.drop_parent.empty() && !cmd.drop_payload.empty() && !playing) {
      const auto child = static_cast<engine::scene::NodeId>(std::strtoul(cmd.drop_payload.c_str(), nullptr, 10));
      if (live_world.valid(child)) {
        sel.Set(child);
        editor::EditorOp op;
        op.kind = editor::EditorOp::Kind::SetParent;
        op.name = cmd.drop_parent;
        (void)editor::ApplyOp(bind_session(), op);
      }
    }
    if (!app_ref.ui_want_capture()) {
      std::string payload;
      if (imgui.PeekDragDrop("content", &payload) && imgui.IsMouseReleased(0)) {
        pending_content = std::atoi(payload.c_str());
        pending_prefab = -1;
      }
    }
    if (cmd.lint) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::Lint;
      const auto r = editor::ApplyOp(bind_session(), op);
      lint_json = r.json;
    }
    if (cmd.bake) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::Bake;
      const auto r = editor::ApplyOp(bind_session(), op);
      if (!r.ok) {
        engine::LogError(r.message);
      }
    }

    if (cmd.sculpt) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::Sculpt;
      op.x = static_cast<float>(settings.tile_x);
      op.z = static_cast<float>(settings.tile_y);
      op.has_y = true;
      op.y = settings.sculpt;
      (void)editor::ApplyOp(bind_session(), op);
      if (settings.heights.size() == 17u * 17u) {
        (void)editor::UploadTerrainMesh(app_ref.device(), app_ref.world(), settings.heights, 2);
      }
    }
    if (cmd.tile_paint) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::PaintTile;
      op.x = static_cast<float>(settings.tile_x);
      op.z = static_cast<float>(settings.tile_y);
      op.sx = static_cast<float>(settings.tile_gid);
      (void)editor::ApplyOp(bind_session(), op);
      editor::SyncStreamer(settings.tiles, &tiles);
    }
    if (cmd.apply_prefab) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::ApplyPrefab;
      (void)editor::ApplyOp(bind_session(), op);
    }
    if (cmd.revert_prefab) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::RevertPrefab;
      (void)editor::ApplyOp(bind_session(), op);
    }

    auto toggle_play = [&](bool voxel_play) {
      if (!playing) {
        playing = true;
        paused_play = false;
        if (voxel_play) {
          voxel.enabled = true;
          (void)mc::SaveWorld(vox, voxel_snap_dir);
          mc::SpawnOnSurface(vox.world, &vox.player);
          mc::GiveSurvivalKit(&vox.player);
          vox.player.creative = false;
          vox.player.flying = false;
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
        rt.entities().Clear();
        rt.set_world(nullptr);
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
      editor::EditorOp op;
      op.kind = playing ? editor::EditorOp::Kind::Stop : editor::EditorOp::Kind::Play;
      if (editor::ApplyOp(bind_session(), op).ok) {
        app_ref.set_move_speed(playing ? 0.f : 5.5f);
      }
    }
    if (vcmd.play) {
      toggle_play(true);
    }
    if (cmd.pause || vcmd.pause) {
      if (playing && !voxel.enabled) {
        editor::EditorOp op;
        op.kind = editor::EditorOp::Kind::Pause;
        (void)editor::ApplyOp(bind_session(), op);
      } else {
        paused_play = !paused_play;
      }
    }
    if (cmd.step && playing && !voxel.enabled) {
      editor::EditorOp op;
      op.kind = editor::EditorOp::Kind::Step;
      (void)editor::ApplyOp(bind_session(), op);
    }
    if (vcmd.step && playing && voxel.enabled) {
      bool one = false;
      mc::GameInput in;
      in.snap = &snap;
      (void)mc::TickGameplay(&vox, app_ref.camera(), in, 1.f / 60.f, &one, &f3_play, voxel_hud_hits);
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
          (void)undo.Undo(app_ref.world(), &meta, &settings);
        }
      }
      if (cmd.redo || (y_down && !y_prev && ctrl)) {
        if (voxel.enabled) {
          (void)voxel_undo.Redo(vox.world);
        } else {
          (void)undo.Redo(app_ref.world(), &meta, &settings);
        }
      }
      if (s_down && !s_prev && ctrl) {
        persist_scene();
      }
      if ((cmd.duplicate || (d_down && !d_prev && ctrl)) && !voxel.enabled) {
        editor::EditorOp op;
        op.kind = editor::EditorOp::Kind::Duplicate;
        (void)editor::ApplyOp(bind_session(), op);
      }
      if ((cmd.destroy || (del_down && !del_prev)) && !voxel.enabled) {
        editor::EditorOp op;
        op.kind = editor::EditorOp::Kind::Destroy;
        (void)editor::ApplyOp(bind_session(), op);
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
      std::vector<game_kit::PrefabDocument> catalog;
      for (const auto& it : content.items) {
        if (it.kind != editor::ContentItem::Kind::Prefab) {
          continue;
        }
        auto loaded = game_kit::LoadPrefabDocument(it.path);
        if (loaded) {
          catalog.push_back(std::move(loaded.value()));
        }
      }
      const auto id = game_kit::InstantiateNested(app_ref.world(), prefab, t, catalog);
      if (app_ref.world().valid(id)) {
        sel.Set(id);
        meta[id] = NodeMeta{prefab.prefab_id,
                            prefab.scene.nodes.empty() ? "" : prefab.scene.nodes[0].script_path};
        std::vector<editor::NodeSnap> snaps;
        editor::CaptureSubtree(app_ref.world(), id, meta, &snaps);
        undo.PushSpawn(std::move(snaps));
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
        const auto player = editor::FindNamed(live_world, "player");
        if (live_world.valid(player)) {
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
          editor::MovePlayerOnGround(live_world, player, app_ref.camera().yaw, wish, 5.5f,
                                    app_ref.delta_time());
          editor::FollowPlayerCamera(&app_ref.camera(), live_world.local_transform(player).position);
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
        const auto rot = app_ref.world().local_transform(sel.node).rotation;
        const bool local = settings.gizmo_local;
        const auto mode = static_cast<editor::GizmoMode>(settings.gizmo_mode);
        const auto hit_ax =
            mode == editor::GizmoMode::Rotate
                ? editor::HitGizmoRotate(ray, origin, editor::kGizmoLength, editor::kGizmoHitRadius,
                                         editor::kGizmoRingRadius, editor::kGizmoRingHit, rot, local)
                : editor::HitGizmoAxes(ray, origin, editor::kGizmoLength, editor::kGizmoHitRadius, rot,
                                       local);
        if (hit_ax != editor::Axis::None) {
          sel.gizmo_axis = static_cast<int>(hit_ax);
          sel.axis_u0 = editor::AxisParamDir(ray, origin, editor::GizmoAxisDir(hit_ax, rot, local));
          sel.drag_acc_x = 0.f;
          sel.drag_acc_z = 0.f;
          store_origins();
          handled = true;
        }
      }
      if (!handled) {
        engine::mixed::PickQuery q;
        engine::render::Camera pick_cam = app_ref.camera();
        float pw = w;
        float ph = h;
        float mx = snap.mouse_x;
        float my = snap.mouse_y;
        if (settings.split_view) {
          editor::ViewportPane panes[4];
          int n = 0;
          editor::LayoutViewports(1, w, h, panes, &n);
          const int pane = editor::PaneAt(panes, n, snap.mouse_x, snap.mouse_y);
          settings.active_pane = pane;
          editor::ApplyPaneCamera(pane, &pick_cam, edit_cam);
          pw = panes[pane].x1 - panes[pane].x0;
          ph = panes[pane].y1 - panes[pane].y0;
          mx = snap.mouse_x - panes[pane].x0;
          my = snap.mouse_y - panes[pane].y0;
        }
        q.screen_px = {mx, my};
        q.viewport_w = pw;
        q.viewport_h = ph;
        q.inv_view_proj = pick_cam.view_proj_matrix(ph > 1.f ? pw / ph : 1.f).Inverse();
        live_world.UpdateTransforms();
        auto pick_scene =
            engine::render::RenderSceneExtractor::Extract(live_world, pick_cam, ph > 1.f ? pw / ph : 1.f);
        std::vector<engine::render2d::Sprite> pick_sprites;
        editor::ExpandTilesToSprites(tiles, &pick_sprites, settings.tile_atlas);
        editor::CollectWorldSprites(live_world, &pick_sprites);
        const auto hit = engine::mixed::Pick(pick_scene.instances, pick_sprites, q);
        if (hit.kind == engine::mixed::PickHit::Kind::Scene3D) {
          if (ctrl) {
            sel.Toggle(hit.node);
          } else {
            sel.Set(hit.node);
          }
          sel.gizmo_axis = 0;
          sel.drag_acc_x = 0.f;
          sel.drag_acc_z = 0.f;
          sel.plane_drag = false;
          store_origins();
        } else if (hit.kind == engine::mixed::PickHit::Kind::Sprite2D) {
          std::vector<engine::scene::NodeId> nodes;
          editor::CollectAllNodes(live_world, &nodes);
          for (auto id : nodes) {
            if (live_world.sprite(id)) {
              sel.Set(id);
              store_origins();
              break;
            }
          }
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
          const auto rot = sel.drag_origins.front().rotation;
          delta = editor::AxisParamDir(ray, sel.drag_origins.front().position,
                                       editor::GizmoAxisDir(static_cast<editor::Axis>(sel.gizmo_axis), rot,
                                                            settings.gizmo_local)) -
                  sel.axis_u0;
        }
        if (editor::ApplyGizmo(app_ref.world(), ids, sel.drag_origins, mode,
                               static_cast<editor::Axis>(sel.gizmo_axis), delta, settings.snap,
                               settings.grid, settings.gizmo_local)) {
          sel.dragging = true;
          settings.dirty = true;
        }
      } else if (!sel.drag_origins.empty()) {
        const auto inv_vp = app_ref.camera().view_proj_matrix(aspect).Inverse();
        const auto ray = editor::ScreenRay(snap.mouse_x, snap.mouse_y, w, h, inv_vp);
        engine::Vec3 hit{};
        const float y = sel.drag_origins.front().position.y;
        if (editor::RayHitYPlane(ray, y, &hit)) {
          if (!sel.plane_drag) {
            sel.plane0 = hit;
            sel.plane_drag = true;
          }
          if (editor::TranslateSelectionDelta(app_ref.world(), ids, sel.drag_origins,
                                              hit.x - sel.plane0.x, hit.z - sel.plane0.z,
                                              settings.snap, settings.grid)) {
            sel.dragging = true;
            settings.dirty = true;
          }
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
      sel.plane_drag = false;
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
      editor::DrawGizmos(app_ref.debug_draw(), live_world, ids,
                         static_cast<editor::GizmoMode>(settings.gizmo_mode), settings.gizmo_local);
    }
    if (!voxel.enabled && settings.show_bounds) {
      live_world.UpdateTransforms();
      std::vector<engine::scene::NodeId> ids = sel.All();
      if (ids.empty()) {
        editor::CollectAllNodes(live_world, &ids);
      }
      editor::DrawBounds(app_ref.debug_draw(), live_world, ids);
    }

    {
      static int last_vp = 0;
      if (settings.split_view) {
        if (!app_ref.ui_want_capture() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
          editor::ViewportPane panes[4];
          int n = 0;
          editor::LayoutViewports(1, w, h, panes, &n);
          settings.active_pane = editor::PaneAt(panes, n, snap.mouse_x, snap.mouse_y);
        }
        editor::ApplyPaneCamera(settings.active_pane, &app_ref.camera(), edit_cam);
        settings.viewport = settings.active_pane;
      } else if (settings.viewport == 0) {
        if (last_vp != 0) {
          app_ref.camera() = edit_cam;
        } else if (!playing) {
          edit_cam = app_ref.camera();
        }
      } else if (settings.viewport == 1) {
        app_ref.camera().position = {0.f, 18.f, 0.01f};
        app_ref.camera().yaw = 0.f;
        app_ref.camera().pitch = -1.55f;
      } else if (settings.viewport == 2) {
        app_ref.camera().position = {0.f, 2.f, 16.f};
        app_ref.camera().yaw = 0.f;
        app_ref.camera().pitch = 0.f;
      } else if (settings.viewport == 3) {
        app_ref.camera().position = {16.f, 2.f, 0.f};
        app_ref.camera().yaw = -1.57f;
        app_ref.camera().pitch = 0.f;
      } else if (settings.viewport == 4) {
        for (const auto& kv : meta) {
          if (kv.second.active_camera && app_ref.world().valid(kv.first)) {
            const auto p = app_ref.world().local_transform(kv.first).position;
            app_ref.camera().position = p + engine::Vec3{0.f, 1.6f, 0.f};
            if (const auto* cam = app_ref.world().camera(kv.first)) {
              app_ref.camera().fovy_rad = cam->fovy_rad;
            } else {
              app_ref.camera().fovy_rad = kv.second.camera_fovy;
            }
            break;
          }
        }
      }
      last_vp = settings.viewport;
    }

    {
      std::vector<engine::render::LocalLight> lights;
      int lid = 1;
      std::vector<engine::scene::NodeId> nodes;
      editor::CollectAllNodes(live_world, &nodes);
      for (auto id : nodes) {
        const auto* Lc = live_world.light(id);
        if (!Lc) {
          continue;
        }
        engine::render::LocalLight L;
        L.id = lid++;
        const auto& wm = live_world.world_matrix(id);
        L.position = {wm.m[12], wm.m[13], wm.m[14]};
        L.range = Lc->range;
        L.intensity = Lc->intensity;
        L.color = {Lc->color.x, Lc->color.y, Lc->color.z, 1.f};
        if (Lc->kind == 1) {
          L.spot_angle_deg = 35.f;
          L.spot_inner_deg = 20.f;
        }
        lights.push_back(L);
      }
      render.set_local_lights(lights);
    }

    if (playing && !voxel.enabled) {
      phys = engine::physics::CreateDefaultPhysicsWorld();
      if (phys) {
        rt.set_physics(phys.get());
        std::vector<engine::scene::NodeId> nodes;
        editor::CollectAllNodes(live_world, &nodes);
        for (auto id : nodes) {
          const auto* col = live_world.collider(id);
          if (!col) {
            continue;
          }
          engine::physics::RigidBodyDesc d;
          d.position = live_world.local_transform(id).position;
          d.half_extents = {col->hx, col->hy, col->hz};
          d.mass = 0.f;
          (void)phys->CreateBox(d);
        }
      }
      if (phys && !paused_play) {
        phys->Step(app_ref.delta_time());
      }
    } else if (phys) {
      rt.set_physics(nullptr);
      phys.reset();
    }

    if (settings.hot_reload) {
      if (assets_hot.Poll() && assets_hot.ConsumeInvalidateRequest()) {
        editor::EditorOp op;
        op.kind = editor::EditorOp::Kind::HotReload;
        (void)editor::ApplyOp(bind_session(), op);
        rescan_content();
      }
      if (playing) {
        for (const auto& kv : meta) {
          if (!kv.second.script_path.empty()) {
            hot.WatchFile(kv.second.script_path);
          }
        }
        if (hot.Poll()) {
          for (const auto& p : hot.changed_files()) {
            (void)play_scripts.ReloadPath(p.string(), true);
          }
        }
      }
    }

    if (settings.show_collision) {
      std::vector<engine::scene::NodeId> nodes;
      editor::CollectAllNodes(live_world, &nodes);
      for (auto id : nodes) {
        const auto* col = live_world.collider(id);
        if (!col) {
          continue;
        }
        const auto p = live_world.local_transform(id).position;
        engine::Aabb box;
        box.min = p - engine::Vec3{col->hx, col->hy, col->hz};
        box.max = p + engine::Vec3{col->hx, col->hy, col->hz};
        app_ref.debug_draw().AddAabb(box, {0.2f, 0.9f, 0.3f, 1.f});
      }
    }
    if (!settings.heights.empty()) {
      const int n = 17;
      for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
          if (settings.heights.size() != static_cast<std::size_t>(n * n)) {
            break;
          }
          const float ht = settings.heights[static_cast<std::size_t>(z * n + x)];
          if (std::fabs(ht) < 0.01f) {
            continue;
          }
          const engine::Vec3 a{static_cast<float>(x - 8), ht, static_cast<float>(z - 8)};
          app_ref.debug_draw().AddLine(a, a + engine::Vec3{0.f, 0.15f, 0.f}, {0.8f, 0.5f, 0.2f, 1.f});
        }
      }
    }
    if (live_world.valid(sel.node)) {
      static float anim_t = 0.f;
      anim_t += app_ref.delta_time();
      const float y = editor::SampleCurve(settings.anim, std::fmod(anim_t, 1.f));
      const auto p = live_world.local_transform(sel.node).position;
      app_ref.debug_draw().AddLine(p, p + engine::Vec3{0.f, 0.25f + y, 0.f}, {1.f, 0.4f, 0.9f, 1.f});
    }
    if (!settings.tiles.empty()) {
      for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
          const int gid = settings.tiles[static_cast<std::size_t>(y * 16 + x)];
          if (gid <= 0) {
            continue;
          }
          const float fx = static_cast<float>(x - 8);
          const float fz = static_cast<float>(y - 8);
          engine::Aabb box;
          box.min = {fx, 0.02f, fz};
          box.max = {fx + 0.9f, 0.04f, fz + 0.9f};
          app_ref.debug_draw().AddAabb(box, {0.3f, 0.4f, 1.f, 1.f});
        }
      }
    }

    if (settings.show_profiler && imgui.BeginWindow("Profiler", 580.f, 12.f, 280.f, 220.f)) {
      for (const auto& kv : profiler.samples_ms()) {
        const std::string line = kv.first + " " + std::to_string(kv.second) + " ms";
        imgui.Text(line.c_str());
      }
      imgui.EndWindow();
    }

    profiler.Begin("DrawFrame");
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
    live_world.UpdateTransforms();
    std::vector<engine::render2d::Sprite> tile_sprites;
    editor::ExpandTilesToSprites(tiles, &tile_sprites, settings.tile_atlas);
    editor::CollectWorldSprites(live_world, &tile_sprites);
    auto submit_view = [&](const engine::render::Camera& cam, float asp,
                           const engine::render::RenderSystem::ColorViewport* vp,
                           const std::vector<engine::rhi::ScreenQuad>* quads) {
      auto draw_scene = engine::render::RenderSceneExtractor::Extract(live_world, cam, asp);
      if (auto st = render.DrawFrame(app_ref.device(), draw_scene, env, asp,
                                     tile_sprites.empty() ? nullptr : &tile_sprites, quads,
                                     &app_ref.debug_draw(), vp);
          !st) {
        engine::LogError(st.message());
      }
    };
    if (settings.split_view && !voxel.enabled) {
      editor::ViewportPane panes[4];
      int n = 0;
      editor::LayoutViewports(1, w, h, panes, &n);
      for (int i = 0; i < n; ++i) {
        engine::render::Camera cam = edit_cam;
        editor::ApplyPaneCamera(i, &cam, edit_cam);
        engine::render::RenderSystem::ColorViewport vp;
        vp.enabled = true;
        vp.x = panes[i].x0;
        vp.y = panes[i].y0;
        vp.w = panes[i].x1 - panes[i].x0;
        vp.h = panes[i].y1 - panes[i].y0;
        vp.skip_post = true;
        const float a = editor::PaneAspect(panes[i]);
        submit_view(cam, a, &vp, nullptr);
      }
      app_ref.device().SetDrawViewport(0.f, 0.f, 0.f, 0.f);
      app_ref.device().SetPreferLdrTarget(false);
    } else {
      auto draw_scene =
          engine::render::RenderSceneExtractor::Extract(live_world, app_ref.camera(), aspect);
      (void)draw_scene;
      submit_view(app_ref.camera(), aspect, nullptr, ui_quads.empty() ? nullptr : &ui_quads);
    }
    profiler.End("DrawFrame");
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
