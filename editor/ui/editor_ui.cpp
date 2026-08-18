#include "editor_ui.h"

#include "editing/snap.h"

#include "engine/core/math.h"

#include <algorithm>
#include <cmath>
#include <string>
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

void CreateGround(engine::scene::World& world, Selection& sel, EditorSettings& settings) {
  const auto id = world.CreateNode("ground");
  engine::scene::Transform t;
  t.scale = {8.f, 1.f, 8.f};
  if (settings.snap) {
    SnapTransform(&t, settings.grid);
  }
  world.set_local_transform(id, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "ground";
  mesh.never_cull = true;
  mesh.local_bounds = {{-4.f, -0.05f, -4.f}, {4.f, 0.05f, 4.f}};
  world.set_mesh(id, mesh);
  sel.Set(id);
  settings.dirty = true;
}

void CreatePlayer(engine::scene::World& world, Selection& sel, EditorSettings& settings) {
  const auto id = world.CreateNode("player");
  engine::scene::Transform t;
  t.position = {0.f, 0.5f, 0.f};
  if (settings.snap) {
    SnapTransform(&t, settings.grid);
  }
  world.set_local_transform(id, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(id, mesh);
  sel.Set(id);
  settings.dirty = true;
}

bool PassesFilter(const engine::scene::World& world, engine::scene::NodeId id, int filter) {
  if (filter == 1) {
    return world.mesh(id) != nullptr;
  }
  if (filter == 2) {
    return world.mesh(id) == nullptr;
  }
  return true;
}

}  // namespace

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
                  ContentBrowser* content, const std::vector<std::string>& scripts, float fps) {
  EditorCommands local{};
  if (!cmd) {
    cmd = &local;
  }
  *cmd = {};

  if (ui.BeginWindow("Hierarchy", 12.f, 12.f, 260.f, 320.f)) {
    ui.Text(settings.dirty ? "DIRTY" : "clean");
    const char* filters[] = {"All", "Mesh", "Empty"};
    (void)ui.Combo("Filter", &settings.hierarchy_filter, filters, 3);
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
      const int depth = i < depths.size() ? depths[i] : 0;
      std::string indent(static_cast<std::size_t>(depth * 2), ' ');
      const std::string raw = world.name(id).empty() ? std::to_string(id) : world.name(id);
      const std::string mark = sel.Contains(id) ? "* " : "  ";
      const std::string label = indent + mark + raw + "##" + std::to_string(id);
      if (ui.Button(label.c_str(), 220.f, 22.f)) {
        sel.Set(id);
      }
    }
    ui.Separator();
    if (ui.Button("Create Cube", 220.f, 24.f)) {
      const auto id = world.CreateNode("cube");
      engine::scene::Transform t;
      t.position = {0.f, 0.5f, 0.f};
      if (settings.snap) {
        SnapTransform(&t, settings.grid);
      }
      world.set_local_transform(id, t);
      engine::scene::MeshRenderer mesh;
      mesh.mesh_id = "cube";
      world.set_mesh(id, mesh);
      sel.Set(id);
      settings.dirty = true;
    }
    if (ui.Button("Create Empty", 220.f, 24.f)) {
      sel.Set(world.CreateNode("empty"));
      settings.dirty = true;
    }
    if (ui.Button("Create Ground", 220.f, 24.f)) {
      CreateGround(world, sel, settings);
    }
    if (ui.Button("Create Player", 220.f, 24.f)) {
      CreatePlayer(world, sel, settings);
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

  if (ui.BeginWindow("Inspector", 12.f, 340.f, 260.f, 300.f)) {
    if (!world.valid(sel.node)) {
      ui.Text("No selection");
    } else {
      auto t = world.local_transform(sel.node);
      ui.Text(world.name(sel.node).c_str());
      const std::string count = "selected " + std::to_string(sel.All().size());
      ui.Text(count.c_str());
      const engine::scene::Transform before = t;
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
        world.set_local_transform(sel.node, t);
        undo.Push(sel.node, before, t);
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
          engine::scene::MeshRenderer mesh;
          mesh.mesh_id = mesh_i == 1 ? "cube" : "ground";
          if (mesh_i == 2) {
            mesh.never_cull = true;
            mesh.local_bounds = {{-4.f, -0.05f, -4.f}, {4.f, 0.05f, 4.f}};
          }
          world.set_mesh(sel.node, mesh);
          settings.dirty = true;
        }
      }
      bool vis = world.visible(sel.node);
      if (ui.Checkbox("Visible", &vis)) {
        world.set_visible(sel.node, vis);
        settings.dirty = true;
      }
      if (ui.Button("Snap to grid", 220.f, 22.f)) {
        auto snapped = t;
        SnapTransform(&snapped, settings.grid);
        world.set_local_transform(sel.node, snapped);
        undo.Push(sel.node, t, snapped);
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
    if (content) {
      ui.Separator();
      ui.Text("Project json");
      for (int i = 0; i < static_cast<int>(content->items.size()); ++i) {
        const auto& it = content->items[static_cast<std::size_t>(i)];
        const std::string lab = (it.kind == ContentItem::Kind::Prefab ? "P " : "S ") + it.label;
        if (ui.Button(lab.c_str(), 220.f, 20.f)) {
          cmd->place_content = i;
        }
      }
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
    ui.EndWindow();
  }

  if (ui.BeginWindow("Settings", 280.f, 540.f, 280.f, 240.f)) {
    const char* modes[] = {"Move", "Rotate", "Scale"};
    (void)ui.Combo("Gizmo", &settings.gizmo_mode, modes, 3);
    ui.Checkbox("Snap", &settings.snap);
    ui.SliderFloat("Grid", &settings.grid, 0.25f, 4.f);
    ui.Checkbox("Show grid", &settings.show_grid);
    ui.Checkbox("Show gizmo", &settings.show_gizmo);
    if (playing && ui.Button("Pause Play", 220.f, 24.f)) {
      cmd->pause = true;
    }
    ui.Text("1/2/3 gizmo  Ctrl+Z/Y undo  Ctrl+D dup  Del  F");
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
