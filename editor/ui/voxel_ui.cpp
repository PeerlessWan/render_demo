#include "voxel_ui.h"

#include <string>

namespace editor {

void DrawVoxelUi(engine::ui::ImmediateUi& ui, VoxelEdit& edit, mc::World& /*world*/, VoxelCommands* cmd,
                 bool playing) {
  VoxelCommands local{};
  if (!cmd) {
    cmd = &local;
  }
  *cmd = {};
  if (!ui.BeginWindow("Voxel", 280.f, 12.f, 280.f, 520.f)) {
    return;
  }
  (void)ui.Checkbox("Voxel mode", &edit.enabled);
  ui.Text(edit.dirty ? "DIRTY" : "clean");
  const std::string dir_text = edit.dir.string();
  ui.Text(dir_text.c_str());
  if (ui.Button("Dir slot0", 220.f, 22.f)) {
    edit.dir_slot = 0;
    edit.dir = "worlds/slot0";
  }
  if (ui.Button("Dir slot1", 220.f, 22.f)) {
    edit.dir_slot = 1;
    edit.dir = "worlds/slot1";
  }
  if (ui.Button("Dir editor", 220.f, 22.f)) {
    edit.dir_slot = 2;
    edit.dir = "worlds/editor";
  }
  if (ui.Button("Save world", 220.f, 24.f)) {
    cmd->save = true;
    edit.dirty = false;
  }
  if (ui.Button("Load world", 220.f, 24.f)) {
    cmd->load = true;
    edit.dirty = false;
  }
  if (ui.Button(playing ? "Stop Play" : "Play voxel", 220.f, 24.f)) {
    cmd->play = true;
  }
  if (playing && ui.Button("Pause Play", 220.f, 24.f)) {
    cmd->pause = true;
  }
  ui.SliderInt("Y layer", &edit.layer_y, 0, 63);
  ui.SliderInt("Brush radius", &edit.brush_radius, 0, 4);
  ui.Checkbox("Box select", &edit.box_mode);
  if (edit.box_has_a) {
    ui.Text("box corner A set");
  }
  if (ui.Button("Fill Y layer", 220.f, 24.f)) {
    cmd->fill_layer = true;
  }
  ui.Separator();
  const mc::Id palette[] = {mc::Id::Stone,         mc::Id::Dirt,          mc::Id::Grass,
                            mc::Id::Sand,          mc::Id::Cobble,        mc::Id::OakLog,
                            mc::Id::OakPlanks,     mc::Id::OakLeaves,     mc::Id::Glass,
                            mc::Id::Water,         mc::Id::CraftingTable, mc::Id::Furnace,
                            mc::Id::Chest,         mc::Id::CoalOre,       mc::Id::IronOre,
                            mc::Id::Bedrock,       mc::Id::Torch,         mc::Id::Bed,
                            mc::Id::Gravel};
  for (mc::Id id : palette) {
    const char* name = mc::GetDef(id).name;
    if (ui.Button(name, 220.f, 22.f)) {
      edit.brush = id;
    }
  }
  const std::string brush_text = std::string("brush: ") + mc::GetDef(edit.brush).name;
  ui.Text(brush_text.c_str());
  ui.EndWindow();
}

}  // namespace editor
