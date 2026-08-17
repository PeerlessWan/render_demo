#include "editor_ui.h"

#include "editing/selection.h"
#include "editing/undo.h"
#include "io/scene_io.h"

#include "engine/core/log.h"

#include <string>
#include <vector>

namespace editor {

void CollectNodes(const engine::scene::World& world, engine::scene::NodeId id,
                  std::vector<engine::scene::NodeId>* out) {
  if (!out || !world.valid(id)) {
    return;
  }
  out->push_back(id);
  for (auto c : world.children(id)) {
    CollectNodes(world, c, out);
  }
}

void DrawEditorUi(engine::ui::ImmediateUi& ui, engine::scene::World& world, Selection& sel,
                  UndoStack& undo, const std::filesystem::path& scene_path, bool* save_clicked,
                  bool* load_clicked, bool* play_clicked, bool playing, std::string* script_path,
                  int* place_prefab) {
  if (save_clicked) {
    *save_clicked = false;
  }
  if (load_clicked) {
    *load_clicked = false;
  }
  if (play_clicked) {
    *play_clicked = false;
  }
  if (place_prefab) {
    *place_prefab = -1;
  }

  if (ui.BeginWindow("Hierarchy", 12.f, 12.f, 260.f, 320.f)) {
    std::vector<engine::scene::NodeId> nodes;
    for (auto r : world.roots()) {
      CollectNodes(world, r, &nodes);
    }
    for (auto id : nodes) {
      const std::string label = world.name(id).empty() ? std::to_string(id) : world.name(id);
      if (ui.Button(label.c_str(), 220.f, 22.f)) {
        sel.node = id;
      }
    }
    ui.Separator();
    if (ui.Button("Create Cube", 220.f, 24.f)) {
      const auto id = world.CreateNode("cube");
      engine::scene::Transform t;
      t.position = {0.f, 0.5f, 0.f};
      world.set_local_transform(id, t);
      engine::scene::MeshRenderer mesh;
      mesh.mesh_id = "cube";
      world.set_mesh(id, mesh);
      sel.node = id;
    }
    if (ui.Button("Create Empty", 220.f, 24.f)) {
      sel.node = world.CreateNode("empty");
    }
    if (ui.Button(playing ? "Stop Play" : "Play scene", 220.f, 24.f) && play_clicked) {
      *play_clicked = true;
    }
    ui.EndWindow();
  }

  if (ui.BeginWindow("Inspector", 12.f, 340.f, 260.f, 260.f)) {
    if (!world.valid(sel.node)) {
      ui.Text("No selection");
    } else {
      auto t = world.local_transform(sel.node);
      ui.Text(world.name(sel.node).c_str());
      const engine::scene::Transform before = t;
      bool changed = false;
      changed = ui.SliderFloat("x", &t.position.x, -20.f, 20.f) || changed;
      changed = ui.SliderFloat("y", &t.position.y, -5.f, 10.f) || changed;
      changed = ui.SliderFloat("z", &t.position.z, -20.f, 20.f) || changed;
      if (changed) {
        world.set_local_transform(sel.node, t);
        undo.Push(sel.node, before, t);
      }
      if (script_path) {
        const std::string sp = script_path->empty() ? "(none)" : *script_path;
        ui.Text(sp.c_str());
        if (ui.Button("Script none", 220.f, 22.f)) {
          *script_path = {};
        }
        if (ui.Button("scripts/chest.lua", 220.f, 22.f)) {
          *script_path = "scripts/chest.lua";
        }
      }
    }
    ui.EndWindow();
  }

  if (ui.BeginWindow("Content", 12.f, 610.f, 260.f, 180.f)) {
    ui.Text(scene_path.string().c_str());
    if (ui.Button("Save", 100.f, 24.f) && save_clicked) {
      *save_clicked = true;
    }
    if (ui.Button("Load", 100.f, 24.f) && load_clicked) {
      *load_clicked = true;
    }
    ui.Separator();
    ui.Text("Prefabs (click then LMB viewport)");
    if (ui.Button("Place chest_tag", 220.f, 22.f) && place_prefab) {
      *place_prefab = 0;
    }
    if (ui.Button("Place tree", 220.f, 22.f) && place_prefab) {
      *place_prefab = 1;
    }
    if (ui.Button("Place hut", 220.f, 22.f) && place_prefab) {
      *place_prefab = 2;
    }
    ui.EndWindow();
  }
}

}  // namespace editor
