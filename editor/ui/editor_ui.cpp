#include "editor_ui.h"

#include "editing/anim_edit.h"
#include "editing/ops.h"
#include "editing/snap.h"

#include "engine/core/math.h"

#include "game_kit/script_fields.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
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
  }
  ui.EndWindow();

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
          const auto empty_meta = std::unordered_map<engine::scene::NodeId, NodeMeta>{};
          const auto& mm = meta ? *meta : empty_meta;
          auto before = CaptureProp(world, sel.node, mm);
          (void)world.set_parent(sel.node, pids[static_cast<std::size_t>(psel)]);
          auto after = CaptureProp(world, sel.node, mm);
          undo.PushProps({std::move(before)}, {std::move(after)});
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
        if (meta) {
          for (auto id : ids) {
            WriteInstanceOverride(world, id, &(*meta)[id]);
          }
        }
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
        std::vector<std::string> mat_names{"(default)"};
        if (content) {
          for (const auto& it : content->items) {
            if (it.asset_id.empty()) {
              continue;
            }
            bool dup = false;
            for (const auto& n : mat_names) {
              if (n == it.asset_id) {
                dup = true;
                break;
              }
            }
            if (!dup) {
              mat_names.push_back(it.asset_id);
            }
          }
        }
        if (mat_names.size() == 1) {
          mat_names.emplace_back("cube");
          mat_names.emplace_back("ground");
        }
        int mat_i = 0;
        for (int i = 1; i < static_cast<int>(mat_names.size()); ++i) {
          if (mm.material_id == mat_names[static_cast<std::size_t>(i)]) {
            mat_i = i;
          }
        }
        std::vector<const char*> mat_items;
        mat_items.reserve(mat_names.size());
        for (const auto& n : mat_names) {
          mat_items.push_back(n.c_str());
        }
        if (ui.Combo("Material", &mat_i, mat_items.data(), static_cast<int>(mat_items.size()))) {
          mm.material_id = mat_i == 0 ? "" : mat_names[static_cast<std::size_t>(mat_i)];
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
          WriteInstanceOverride(world, sel.node, &mm);
        }
        if (ui.Checkbox("Light", &mm.has_light)) {
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
          WriteInstanceOverride(world, sel.node, &mm);
        }
        if (mm.has_light) {
          bool light_ch = false;
          light_ch = ui.SliderInt("Kind", &mm.light_kind, 0, 2) || light_ch;
          light_ch = ui.SliderFloat("Range", &mm.light_range, 1.f, 32.f) || light_ch;
          light_ch = ui.SliderFloat("Intensity", &mm.light_intensity, 0.f, 8.f) || light_ch;
          light_ch = ui.SliderFloat("LcR", &mm.light_r, 0.f, 1.f) || light_ch;
          light_ch = ui.SliderFloat("LcG", &mm.light_g, 0.f, 1.f) || light_ch;
          light_ch = ui.SliderFloat("LcB", &mm.light_b, 0.f, 1.f) || light_ch;
          if (light_ch) {
            settings.dirty = true;
            SyncMetaToWorld(world, *meta);
            WriteInstanceOverride(world, sel.node, &mm);
          }
        }
        if (ui.Checkbox("Camera", &mm.has_camera)) {
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
        }
        if (mm.has_camera) {
          if (ui.Checkbox("Active cam", &mm.active_camera)) {
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
          if (ui.SliderFloat("fovy", &mm.camera_fovy, 0.3f, 1.8f)) {
            settings.dirty = true;
            SyncMetaToWorld(world, *meta);
          }
        }
        if (ui.Checkbox("Collider", &mm.has_collider)) {
          settings.dirty = true;
          SyncMetaToWorld(world, *meta);
        }
        if (mm.has_collider) {
          bool col_ch = false;
          col_ch = ui.SliderFloat("hx", &mm.collider_hx, 0.1f, 4.f) || col_ch;
          col_ch = ui.SliderFloat("hy", &mm.collider_hy, 0.1f, 4.f) || col_ch;
          col_ch = ui.SliderFloat("hz", &mm.collider_hz, 0.1f, 4.f) || col_ch;
          if (col_ch) {
            settings.dirty = true;
            SyncMetaToWorld(world, *meta);
          }
        }
        bool has_spr = world.sprite(sel.node) != nullptr;
        if (ui.Checkbox("Sprite", &has_spr)) {
          if (has_spr) {
            engine::scene::SpriteComponent spr;
            world.set_sprite(sel.node, spr);
          } else {
            world.clear_sprite(sel.node);
          }
          settings.dirty = true;
        }
        if (const auto* spr = world.sprite(sel.node)) {
          auto copy = *spr;
          if (ui.SliderInt("gid", &copy.gid, 0, 16)) {
            world.set_sprite(sel.node, copy);
            settings.dirty = true;
          }
        }
        {
          std::string src;
          const auto& sp = mm.script_path.empty() && script_path ? *script_path : mm.script_path;
          if (!sp.empty()) {
            std::ifstream in(sp, std::ios::binary);
            if (in) {
              src.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
          }
          auto fields = game_kit::ParseScriptExports(src);
          game_kit::OverlayPersistBlob(&fields, mm.script_fields);
          if (!fields.empty() && src.find("--@export") != std::string::npos) {
            bool fchg = false;
            for (auto& f : fields) {
              if (f.type == "number") {
                float v = std::strtof(f.value.c_str(), nullptr);
                if (ui.SliderFloat(f.name.c_str(), &v, -32.f, 32.f)) {
                  std::ostringstream oss;
                  oss << v;
                  f.value = oss.str();
                  fchg = true;
                }
              } else if (f.type == "bool") {
                bool b = f.value == "true" || f.value == "1";
                if (ui.Checkbox(f.name.c_str(), &b)) {
                  f.value = b ? "true" : "false";
                  fchg = true;
                }
              } else {
                char buf[128]{};
                std::memcpy(buf, f.value.c_str(), std::min(f.value.size(), sizeof(buf) - 1));
                if (ui.InputText(f.name.c_str(), buf, sizeof(buf))) {
                  f.value = buf;
                  fchg = true;
                }
              }
            }
            if (fchg) {
              mm.script_fields = game_kit::FieldsToPersist(fields);
              settings.dirty = true;
              WriteInstanceOverride(world, sel.node, &mm);
            }
          } else {
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
          }
        }
        int anim_i = 0;
        for (int i = 0; i < static_cast<int>(settings.anim.states.size()); ++i) {
          if (mm.anim_state == settings.anim.states[static_cast<std::size_t>(i)]) {
            anim_i = i + 1;
          }
        }
        std::vector<std::string> anim_names{"(none)"};
        anim_names.insert(anim_names.end(), settings.anim.states.begin(), settings.anim.states.end());
        std::vector<const char*> anim_items;
        for (const auto& n : anim_names) {
          anim_items.push_back(n.c_str());
        }
        if (ui.Combo("Anim", &anim_i, anim_items.data(), static_cast<int>(anim_items.size()))) {
          mm.anim_state = anim_i == 0 ? "" : anim_names[static_cast<std::size_t>(anim_i)];
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
  }
  ui.EndWindow();

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
    if (content) {
      const bool list_open = ui.BeginChild("content_list", 240.f, 120.f);
      if (list_open) {
        for (int i = 0; i < static_cast<int>(content->items.size()); ++i) {
          const auto& it = content->items[static_cast<std::size_t>(i)];
          ui.ColorBox(it.thumb_r, it.thumb_g, it.thumb_b, 1.f, 16.f, 16.f);
          if (!it.thumb_px.empty()) {
            const int tw = std::max(1, it.thumb_w);
            const int n = std::min(4, tw);
            for (int px = 0; px < n; ++px) {
              const std::size_t o = static_cast<std::size_t>(px) * 4;
              if (o + 2 < it.thumb_px.size()) {
                ui.SameLine();
                ui.ColorBox(it.thumb_px[o] / 255.f, it.thumb_px[o + 1] / 255.f,
                            it.thumb_px[o + 2] / 255.f, 1.f, 4.f, 16.f);
              }
            }
          }
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
  }
  ui.EndWindow();

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
    const char* brushes[] = {"Raise", "Lower", "Smooth"};
    (void)ui.Combo("Brush", &settings.sculpt_mode, brushes, 3);
    (void)ui.SliderFloat("Sculpt", &settings.sculpt, 0.05f, 1.f);
    if (ui.Button("Sculpt apply", 220.f, 22.f)) {
      cmd->sculpt = true;
    }
    static char atlas_buf[64]{};
    static bool atlas_init = false;
    if (!atlas_init) {
      std::memcpy(atlas_buf, settings.tile_atlas.c_str(),
                  std::min(settings.tile_atlas.size(), sizeof(atlas_buf) - 1));
      atlas_init = true;
    }
    if (ui.InputText("Atlas", atlas_buf, sizeof(atlas_buf))) {
      settings.tile_atlas = atlas_buf;
      settings.dirty = true;
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
  }
  ui.EndWindow();

  if (ui.BeginWindow("Anim / Bake", 580.f, 540.f, 280.f, 260.f)) {
    ui.Text("State graph");
    std::vector<const char*> anims;
    for (const auto& s : settings.anim.states) {
      anims.push_back(s.c_str());
    }
    if (!anims.empty()) {
      if (ui.Combo("State", &settings.anim.current, anims.data(), static_cast<int>(anims.size()))) {
        cmd->anim_state = settings.anim.current;
      }
    }
    static char new_state[32]{"jump"};
    if (ui.InputText("New state", new_state, sizeof(new_state))) {
    }
    if (ui.Button("Add state", 105.f, 22.f)) {
      auto a0 = settings.anim;
      AddState(&settings.anim, new_state);
      undo.PushGrid(settings.heights, settings.tiles, a0, settings.heights, settings.tiles,
                    settings.anim);
      settings.dirty = true;
    }
    if (ui.Button("Remove state", 105.f, 22.f)) {
      auto a0 = settings.anim;
      RemoveState(&settings.anim, settings.anim.current);
      undo.PushGrid(settings.heights, settings.tiles, a0, settings.heights, settings.tiles,
                    settings.anim);
      settings.dirty = true;
    }
    static char from_st[32]{"idle"};
    static char to_st[32]{"walk"};
    (void)ui.InputText("From", from_st, sizeof(from_st));
    (void)ui.InputText("To", to_st, sizeof(to_st));
    if (ui.Button("Add transition", 220.f, 22.f)) {
      auto a0 = settings.anim;
      AddTransition(&settings.anim, from_st, to_st);
      undo.PushGrid(settings.heights, settings.tiles, a0, settings.heights, settings.tiles,
                    settings.anim);
      settings.dirty = true;
    }
    ui.Text("Curve keys 0..1");
    (void)ui.SliderFloat("k0", &settings.anim.keys[0], 0.f, 1.f);
    (void)ui.SliderFloat("k1", &settings.anim.keys[1], 0.f, 1.f);
    (void)ui.SliderFloat("k2", &settings.anim.keys[2], 0.f, 1.f);
    (void)ui.SliderFloat("k3", &settings.anim.keys[3], 0.f, 1.f);
    if (ui.Button("Lint graph", 220.f, 22.f)) {
      cmd->lint = true;
    }
  }
  ui.EndWindow();

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
  }
  ui.EndWindow();
}

}  // namespace editor
