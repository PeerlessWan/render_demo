#include "editor_ui.h"

#include "editing/ops.h"
#include "editing/snap.h"

#include "engine/core/math.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {
namespace {

void EulerFromQuat(const engine::Quat& q, float* yaw, float* pitch, float* roll) {
  const float y = std::atan2(2.f * (q.w * q.y + q.x * q.z), 1.f - 2.f * (q.y * q.y + q.x * q.x));
  const float p = std::asin(std::clamp(2.f * (q.w * q.x - q.z * q.y), -1.f, 1.f));
  const float r = std::atan2(2.f * (q.w * q.z + q.x * q.y), 1.f - 2.f * (q.x * q.x + q.z * q.z));
  if (yaw) {
    *yaw = y;
  }
  if (pitch) {
    *pitch = p;
  }
  if (roll) {
    *roll = r;
  }
}

}  // namespace

bool PassesFilter(const engine::scene::World& world, engine::scene::NodeId id, int filter) {
  if (filter == 1) {
    return world.mesh(id) != nullptr;
  }
  if (filter == 2) {
    return world.mesh(id) == nullptr;
  }
  return true;
}

void CollectNodes(const engine::scene::World& world, engine::scene::NodeId id,
                  std::vector<engine::scene::NodeId>* out) {
  std::vector<int> depths;
  CollectNodesDeep(world, id, out, &depths, 0);
}

void CollectNodesDeep(const engine::scene::World& world, engine::scene::NodeId id,
                      std::vector<engine::scene::NodeId>* out, std::vector<int>* depths, int depth) {
  if (!out || !world.valid(id)) {
    return;
  }
  out->push_back(id);
  if (depths) {
    depths->push_back(depth);
  }
  for (auto c : world.children(id)) {
    CollectNodesDeep(world, c, out, depths, depth + 1);
  }
}

void DrawEditorUi(engine::ui::ImmediateUi& ui, engine::scene::World& world, Selection& sel,
                  UndoStack& undo, const std::filesystem::path& scene_path, EditorSettings& settings,
                  EditorCommands* cmd, std::string* script_path, bool playing, bool paused,
                  ContentBrowser* content, const std::vector<std::string>& scripts, float fps,
                  bool multi_mod, std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  EditorCommands local{};
  if (!cmd) {
    cmd = &local;
  }
  *cmd = {};

  if (ui.BeginWindow("Hierarchy", 12.f, 12.f, 260.f, 320.f)) {
    ui.Text(settings.dirty ? "DIRTY" : "clean");
    const char* spaces[] = {"Scene", "Voxel"};
    (void)ui.Combo("Workspace", &settings.workspace, spaces, 2);
    const char* filters[] = {"All", "Mesh", "Empty"};
    (void)ui.Combo("Filter", &settings.hierarchy_filter, filters, 3);
    (void)ui.InputText("Search", settings.search, sizeof(settings.search));
    std::vector<engine::scene::NodeId> nodes;
    std::vector<int> depths;
    for (auto r : world.roots()) {
      CollectNodesDeep(world, r, &nodes, &depths, 0);
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const auto id = nodes[i];
      if (!PassesFilter(world, id, settings.hierarchy_filter)) {
        continue;
      }
      if (settings.search[0] != 0) {
        const std::string rawn = world.name(id);
        if (rawn.find(settings.search) == std::string::npos) {
          continue;
        }
      }
      const int depth = i < depths.size() ? depths[i] : 0;
      std::string indent(static_cast<std::size_t>(depth * 2), ' ');
      const std::string raw = world.name(id).empty() ? std::to_string(id) : world.name(id);
      const std::string mark = sel.Contains(id) ? "* " : "  ";
      const auto parent = FindParent(world, id);
      std::string pname = "-";
      if (world.valid(parent)) {
        pname = world.name(parent).empty() ? std::to_string(parent) : world.name(parent);
      }
      bool vis = world.visible(id);
      const std::string vis_lab = std::string("v##") + std::to_string(id);
      if (ui.Checkbox(vis_lab.c_str(), &vis)) {
        const auto empty_meta = std::unordered_map<engine::scene::NodeId, NodeMeta>{};
        const auto& mm = meta ? *meta : empty_meta;
        auto before = CaptureProp(world, id, mm);
        world.set_visible(id, vis);
        auto after = CaptureProp(world, id, mm);
        undo.PushProps({before}, {after});
        settings.dirty = true;
      }
      const std::string label = indent + mark + raw + " <" + pname + ">##" + std::to_string(id);
      if (ui.Selectable(label.c_str(), sel.Contains(id))) {
        if (multi_mod) {
          sel.Toggle(id);
        } else {
          sel.Set(id);
        }
      }
      if (ui.BeginDragDropSource()) {
        ui.SetDragDropPayload("node", std::to_string(id));
        ui.EndDragDropSource();
      }
      if (ui.BeginDragDropTarget()) {
        std::string payload;
        if (ui.AcceptDragDropPayload("node", &payload)) {
          cmd->drop_parent = world.name(id);
          cmd->drop_payload = payload;
        }
        ui.EndDragDropTarget();
      }
    }
    ui.Separator();
    if (ui.Button("Create Cube", 220.f, 24.f)) {
      cmd->create_kind = 0;
    }
    if (ui.Button("Create Empty", 220.f, 24.f)) {
      cmd->create_kind = 1;
    }
    if (ui.Button("Create Ground", 220.f, 24.f)) {
      cmd->create_kind = 2;
    }
    if (ui.Button("Create Player", 220.f, 24.f)) {
      cmd->create_kind = 3;
    }
    if (ui.Button("Create Light", 220.f, 24.f)) {
      cmd->create_kind = 4;
    }
    if (ui.Button("Create Camera", 220.f, 24.f)) {
      cmd->create_kind = 5;
    }
    if (ui.Button("Create Collider", 220.f, 24.f)) {
      cmd->create_kind = 6;
    }
    if (ui.Button("Create Sprite", 220.f, 24.f)) {
      cmd->create_kind = 7;
    }
    if (ui.Button("Duplicate", 220.f, 24.f)) {
      cmd->duplicate = true;
    }
    if (ui.Button("Delete", 220.f, 24.f)) {
      cmd->destroy = true;
    }
    if (ui.Button(playing ? "Stop Play" : "Play scene", 220.f, 24.f)) {
      cmd->play = true;
    }
    ui.EndWindow();
  }

  if (ui.BeginWindow("Inspector", 12.f, 340.f, 260.f, 420.f)) {
    if (!world.valid(sel.node)) {
      ui.Text("No selection");
    } else {
      auto t = world.local_transform(sel.node);
      ui.Text(world.name(sel.node).c_str());
      static engine::scene::NodeId named = engine::scene::kInvalidNode;
      static char name_buf[64]{};
      if (named != sel.node) {
        named = sel.node;
        std::memset(name_buf, 0, sizeof(name_buf));
        const auto& nm = world.name(sel.node);
        std::memcpy(name_buf, nm.c_str(), std::min(nm.size(), sizeof(name_buf) - 1));
      }
      if (ui.InputText("Name", name_buf, sizeof(name_buf))) {
        const auto empty_meta = std::unordered_map<engine::scene::NodeId, NodeMeta>{};
        const auto& mm = meta ? *meta : empty_meta;
        auto before = CaptureProp(world, sel.node, mm);
        world.set_name(sel.node, name_buf);
        auto after = CaptureProp(world, sel.node, mm);
        undo.PushProps({before}, {after});
        settings.dirty = true;
      }
      {
        std::vector<engine::scene::NodeId> all;
        for (auto r : world.roots()) {
          CollectNodesDeep(world, r, &all, nullptr, 0);
        }
        std::vector<std::string> plabels;
        plabels.emplace_back("(root)");
        std::vector<engine::scene::NodeId> pids{engine::scene::kInvalidNode};
        const auto cur_p = FindParent(world, sel.node);
        int psel = 0;
        for (auto id : all) {
          if (id == sel.node) {
            continue;
          }
          pids.push_back(id);
          const std::string n = world.name(id).empty() ? std::to_string(id) : world.name(id);
          plabels.push_back(n);
          if (id == cur_p) {
            psel = static_cast<int>(pids.size() - 1);
          }
        }
        std::vector<const char*> pitems;
        pitems.reserve(plabels.size());
        for (const auto& s : plabels) {
          pitems.push_back(s.c_str());
        }
        if (!pitems.empty() &&
            ui.Combo("Parent", &psel, pitems.data(), static_cast<int>(pitems.size()))) {
          (void)world.set_parent(sel.node, pids[static_cast<std::size_t>(psel)]);
          settings.dirty = true;
        }
      }
      const auto ids = sel.All();
      const std::string count = "selected " + std::to_string(ids.size());
      ui.Text(count.c_str());
      bool mixed = false;
      for (auto id : ids) {
        const auto& o = world.local_transform(id);
        if (std::fabs(o.position.x - t.position.x) > 1e-4f ||
            std::fabs(o.position.y - t.position.y) > 1e-4f ||
            std::fabs(o.position.z - t.position.z) > 1e-4f) {
          mixed = true;
        }
      }
      if (mixed) {
        ui.Text("mixed");
      }
      std::vector<engine::scene::Transform> befores;
      befores.reserve(ids.size());
      for (auto id : ids) {
        befores.push_back(world.local_transform(id));
      }
      bool changed = false;
      changed = ui.SliderFloat("x", &t.position.x, -20.f, 20.f) || changed;
      changed = ui.SliderFloat("y", &t.position.y, -5.f, 10.f) || changed;
      changed = ui.SliderFloat("z", &t.position.z, -20.f, 20.f) || changed;
      float yaw = 0.f;
      float pitch = 0.f;
      float roll = 0.f;
      EulerFromQuat(t.rotation, &yaw, &pitch, &roll);
      bool rot = false;
      rot = ui.SliderFloat("yaw", &yaw, -3.14f, 3.14f) || rot;
      rot = ui.SliderFloat("pitch", &pitch, -1.57f, 1.57f) || rot;
      rot = ui.SliderFloat("roll", &roll, -3.14f, 3.14f) || rot;
      if (rot) {
        t.rotation = engine::Quat::FromEulerYxz(yaw, pitch, roll);
        changed = true;
      }
      changed = ui.SliderFloat("sx", &t.scale.x, 0.1f, 8.f) || changed;
      changed = ui.SliderFloat("sy", &t.scale.y, 0.1f, 8.f) || changed;
      changed = ui.SliderFloat("sz", &t.scale.z, 0.1f, 8.f) || changed;
      if (changed) {
        if (settings.snap) {
          SnapTransform(&t, settings.grid);
        }
        std::vector<engine::scene::Transform> afters;
        afters.reserve(ids.size());
        for (auto id : ids) {
          world.set_local_transform(id, t);
          afters.push_back(t);
        }
        undo.PushBatch(ids, befores, afters);
        settings.dirty = true;
      }
      int mesh_i = 0;
      if (const auto* mesh = world.mesh(sel.node)) {
        if (mesh->mesh_id == "cube") {
          mesh_i = 1;
        } else if (mesh->mesh_id == "ground") {
          mesh_i = 2;
        }
      }
      const char* meshes[] = {"(none)", "cube", "ground"};
      if (ui.Combo("Mesh", &mesh_i, meshes, 3)) {
        if (mesh_i == 1 || mesh_i == 2) {
          const auto empty_meta = std::unordered_map<engine::scene::NodeId, NodeMeta>{};
          const auto& mm = meta ? *meta : empty_meta;
          std::vector<PropSnap> pb;
          std::vector<PropSnap> pa;
          engine::scene::MeshRenderer mesh;
          mesh.mesh_id = mesh_i == 1 ? "cube" : "ground";
          if (mesh_i == 2) {
            mesh.never_cull = true;
            mesh.local_bounds = {{-4.f, -0.05f, -4.f}, {4.f, 0.05f, 4.f}};
          }
          for (auto id : ids) {
            pb.push_back(CaptureProp(world, id, mm));
            world.set_mesh(id, mesh);
            pa.push_back(CaptureProp(world, id, mm));
          }
          undo.PushProps(std::move(pb), std::move(pa));
          settings.dirty = true;
        }
      }
      bool vis = world.visible(sel.node);
      if (ui.Checkbox("Visible", &vis)) {
        const auto empty_meta = std::unordered_map<engine::scene::NodeId, NodeMeta>{};
        const auto& mm = meta ? *meta : empty_meta;
        std::vector<PropSnap> pb;
        std::vector<PropSnap> pa;
        for (auto id : ids) {
          pb.push_back(CaptureProp(world, id, mm));
          world.set_visible(id, vis);
          pa.push_back(CaptureProp(world, id, mm));
        }
        undo.PushProps(std::move(pb), std::move(pa));
        settings.dirty = true;
      }
      if (meta) {
        auto& mm = (*meta)[sel.node];
        int mat_i = 0;
        if (mm.material_id == "cube") {
          mat_i = 1;
        } else if (mm.material_id == "ground") {
          mat_i = 2;
        }
        const char* mats[] = {"(default)", "cube", "ground"};
        if (ui.Combo("Material", &mat_i, mats, 3)) {
          mm.material_id = mat_i == 0 ? "" : mats[mat_i];
          settings.dirty = true;
        }
        if (ui.Checkbox("Light", &mm.has_light)) {
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
        }
        if (mm.has_light) {
          (void)ui.SliderFloat("Range", &mm.light_range, 1.f, 32.f);
          (void)ui.SliderFloat("Intensity", &mm.light_intensity, 0.f, 8.f);
          SyncMetaToWorld(world, *meta);
        }
        if (ui.Checkbox("Camera", &mm.has_camera)) {
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
        }
        if (mm.has_camera && ui.Checkbox("Active cam", &mm.active_camera)) {
          if (mm.active_camera) {
            for (auto& kv : *meta) {
              if (kv.first != sel.node) {
                kv.second.active_camera = false;
              }
            }
          }
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
        }
        if (ui.Checkbox("Collider", &mm.has_collider)) {
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
        }
        if (mm.has_collider) {
          (void)ui.SliderFloat("hx", &mm.collider_hx, 0.1f, 4.f);
          (void)ui.SliderFloat("hy", &mm.collider_hy, 0.1f, 4.f);
          (void)ui.SliderFloat("hz", &mm.collider_hz, 0.1f, 4.f);
        }
        static char fields_buf[256]{};
        static engine::scene::NodeId fields_id = engine::scene::kInvalidNode;
        if (fields_id != sel.node) {
          fields_id = sel.node;
          std::memset(fields_buf, 0, sizeof(fields_buf));
          std::memcpy(fields_buf, mm.script_fields.c_str(),
                      std::min(mm.script_fields.size(), sizeof(fields_buf) - 1));
        }
        if (ui.InputText("Fields", fields_buf, sizeof(fields_buf))) {
          mm.script_fields = fields_buf;
          settings.dirty = true;
        }
        int anim_i = 0;
        if (mm.anim_state == "walk") {
          anim_i = 1;
        } else if (mm.anim_state == "run") {
          anim_i = 2;
        }
        const char* anims[] = {"(none)", "walk", "run"};
        if (ui.Combo("Anim", &anim_i, anims, 3)) {
          mm.anim_state = anim_i == 0 ? "" : anims[anim_i];
          settings.dirty = true;
        }
        if (!mm.prefab_id.empty() || !mm.source_prefab.empty()) {
          ui.Text(mm.prefab_id.empty() ? mm.source_prefab.c_str() : mm.prefab_id.c_str());
          if (ui.Button("Apply prefab", 220.f, 22.f)) {
            cmd->apply_prefab = true;
          }
          if (ui.Button("Revert prefab", 220.f, 22.f)) {
            cmd->revert_prefab = true;
          }
        }
      }
      if (ui.Button("Snap to grid", 220.f, 22.f)) {
        auto snapped = t;
        SnapTransform(&snapped, settings.grid);
        std::vector<engine::scene::Transform> afters;
        for (auto id : ids) {
          world.set_local_transform(id, snapped);
          afters.push_back(snapped);
        }
        undo.PushBatch(ids, befores, afters);
        settings.dirty = true;
      }
      if (ui.Button("Frame camera", 220.f, 22.f)) {
        cmd->frame = true;
      }
      if (script_path) {
        int si = 0;
        std::vector<std::string> labels;
        labels.push_back("(none)");
        for (const auto& p : scripts) {
          labels.push_back(p);
        }
        if (!script_path->empty()) {
          bool found = false;
          for (std::size_t i = 0; i < scripts.size(); ++i) {
            if (scripts[i] == *script_path) {
              si = static_cast<int>(i + 1);
              found = true;
              break;
            }
          }
          if (!found) {
            labels.push_back(*script_path);
            si = static_cast<int>(labels.size() - 1);
          }
        }
        std::vector<const char*> items;
        items.reserve(labels.size());
        for (const auto& s : labels) {
          items.push_back(s.c_str());
        }
        if (ui.Combo("Script", &si, items.data(), static_cast<int>(items.size()))) {
          if (si <= 0) {
            *script_path = {};
          } else {
            *script_path = labels[static_cast<std::size_t>(si)];
          }
          if (meta) {
            const auto empty_meta = std::unordered_map<engine::scene::NodeId, NodeMeta>{};
            std::vector<PropSnap> pb;
            std::vector<PropSnap> pa;
            for (auto id : ids) {
              pb.push_back(CaptureProp(world, id, *meta));
              (*meta)[id].script_path = *script_path;
              pa.push_back(CaptureProp(world, id, *meta));
            }
            undo.PushProps(std::move(pb), std::move(pa));
          }
          settings.dirty = true;
        }
      }
    }
    ui.EndWindow();
  }

  if (ui.BeginWindow("Content", 12.f, 648.f, 260.f, 220.f)) {
    ui.Text(scene_path.string().c_str());
    if (ui.Button("Save", 100.f, 24.f)) {
      cmd->save = true;
    }
    if (ui.Button("Load", 100.f, 24.f)) {
      cmd->load = true;
    }
    if (ui.Button("Undo", 100.f, 22.f)) {
      cmd->undo = true;
    }
    if (ui.Button("Redo", 100.f, 22.f)) {
      cmd->redo = true;
    }
    ui.Separator();
    ui.Text("Prefabs (click then LMB viewport)");
    if (ui.Button("Place chest_tag", 220.f, 22.f)) {
      cmd->place_prefab = 0;
    }
    if (ui.Button("Place tree", 220.f, 22.f)) {
      cmd->place_prefab = 1;
    }
    if (ui.Button("Place hut", 220.f, 22.f)) {
      cmd->place_prefab = 2;
    }
    ui.Separator();
    ui.Text("Project (drag prefab to viewport)");
    if (content && ui.BeginChild("content_list", 240.f, 120.f)) {
      for (int i = 0; i < static_cast<int>(content->items.size()); ++i) {
        const auto& it = content->items[static_cast<std::size_t>(i)];
        ui.ColorBox(it.thumb_r, it.thumb_g, it.thumb_b, 1.f, 16.f, 16.f);
        ui.SameLine();
        const std::string lab = (it.kind == ContentItem::Kind::Scene ? "S " : "P ") + it.label +
                                (it.asset_id.empty() ? "" : " [" + it.asset_id + "]");
        if (it.kind == ContentItem::Kind::Scene) {
          if (ui.Selectable(lab.c_str(), false)) {
            cmd->open_scene = i;
          }
        } else {
          if (ui.Selectable(lab.c_str(), content->pending == i)) {
            cmd->place_content = i;
          }
          if (ui.BeginDragDropSource()) {
            ui.SetDragDropPayload("content", std::to_string(i));
            ui.EndDragDropSource();
          }
        }
      }
      ui.EndChild();
    }
    if (ui.Button("Save Prefab from selection", 220.f, 22.f)) {
      cmd->save_prefab = true;
    }
    if (ui.Button("Rescan content", 220.f, 22.f)) {
      cmd->rescan = true;
    }
    if (ui.Button("Cook", 220.f, 22.f)) {
      cmd->cook = true;
    }
    if (ui.Button("Bake lightmap", 220.f, 22.f)) {
      cmd->bake = true;
    }
    if (ui.Button("Lint (C20)", 220.f, 22.f)) {
      cmd->lint = true;
    }
    ui.EndWindow();
  }

  if (ui.BeginWindow("Settings", 280.f, 540.f, 280.f, 380.f)) {
    const char* modes[] = {"Move", "Rotate", "Scale"};
    (void)ui.Combo("Gizmo", &settings.gizmo_mode, modes, 3);
    ui.Checkbox("Local gizmo", &settings.gizmo_local);
    ui.Checkbox("Snap", &settings.snap);
    ui.SliderFloat("Grid", &settings.grid, 0.25f, 4.f);
    ui.Checkbox("Show grid", &settings.show_grid);
    ui.Checkbox("Show gizmo", &settings.show_gizmo);
    ui.Checkbox("Show bounds", &settings.show_bounds);
    ui.Checkbox("Show collision", &settings.show_collision);
    ui.Checkbox("Show profiler", &settings.show_profiler);
    ui.Checkbox("Hot reload", &settings.hot_reload);
    const char* views[] = {"Persp", "Top", "Front", "Side", "Node cam"};
    (void)ui.Combo("Viewport", &settings.viewport, views, 5);
    ui.Checkbox("Split 2x2", &settings.split_view);
    (void)ui.SliderInt("Pane", &settings.active_pane, 0, 3);
    (void)ui.SliderFloat("Sculpt", &settings.sculpt, 0.05f, 1.f);
    if (ui.Button("Sculpt raise", 220.f, 22.f)) {
      cmd->sculpt = true;
    }
    (void)ui.SliderInt("Tile X", &settings.tile_x, 0, 15);
    (void)ui.SliderInt("Tile Y", &settings.tile_y, 0, 15);
    (void)ui.SliderInt("GID", &settings.tile_gid, 0, 8);
    if (ui.Button("Paint tile", 220.f, 22.f)) {
      cmd->tile_paint = true;
    }
    if (playing && ui.Button(paused ? "Resume Play" : "Pause Play", 220.f, 24.f)) {
      cmd->pause = true;
    }
    if (playing && paused && ui.Button("Step", 220.f, 24.f)) {
      cmd->step = true;
    }
    ui.Text("1/2/3 gizmo  Ctrl+Z/Y undo  Ctrl+D dup  Del  F");
    ui.EndWindow();
  }

  if (ui.BeginWindow("Anim / Bake", 580.f, 540.f, 280.f, 220.f)) {
    ui.Text("State graph");
    const char* anims[] = {"idle", "walk", "run"};
    static int anim_i = 0;
    if (ui.Combo("State", &anim_i, anims, 3)) {
      cmd->anim_state = anim_i;
    }
    ui.Text("Curve keys 0..1");
    (void)ui.SliderFloat("k0", &settings.sculpt, 0.05f, 1.f);
    if (ui.Button("Lint graph", 220.f, 22.f)) {
      cmd->lint = true;
    }
    ui.EndWindow();
  }

  if (ui.BeginWindow("Debug", 280.f, 790.f, 280.f, 120.f)) {
    const std::string n = "nodes " + std::to_string(world.roots().size());
    ui.Text(n.c_str());
    if (world.valid(sel.node)) {
      ui.Text(world.name(sel.node).c_str());
    } else {
      ui.Text("(no selection)");
    }
    ui.Text(playing ? (paused ? "Play PAUSED" : "Play RUN") : "Edit");
    const std::string fps_s = "fps " + std::to_string(static_cast<int>(fps + 0.5f));
    ui.Text(fps_s.c_str());
    ui.EndWindow();
  }
}

}  // namespace editor
