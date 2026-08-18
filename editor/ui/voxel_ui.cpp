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
  const bool open = ui.BeginWindow("体素", 280.f, 12.f, 280.f, 520.f);
  if (!open) {
    ui.EndWindow();
    return;
  }
  (void)ui.Checkbox("体素模式", &edit.enabled);
  ui.Text(edit.dirty ? "已修改" : "未改");
  const std::string dir_text = edit.dir.string();
  ui.Text(dir_text.c_str());
  if (ui.Button("目录 slot0", 220.f, 22.f)) {
    edit.dir_slot = 0;
    edit.dir = "worlds/slot0";
  }
  if (ui.Button("目录 slot1", 220.f, 22.f)) {
    edit.dir_slot = 1;
    edit.dir = "worlds/slot1";
  }
  if (ui.Button("目录 editor", 220.f, 22.f)) {
    edit.dir_slot = 2;
    edit.dir = "worlds/editor";
  }
  if (ui.Button("保存世界", 220.f, 24.f)) {
    cmd->save = true;
    edit.dirty = false;
  }
  if (ui.Button("加载世界", 220.f, 24.f)) {
    cmd->load = true;
    edit.dirty = false;
  }
  if (ui.Button(playing ? "停止播放" : "播放体素", 220.f, 24.f)) {
    cmd->play = true;
  }
  if (playing && ui.Button("暂停播放", 220.f, 24.f)) {
    cmd->pause = true;
  }
  if (playing && ui.Button("单步", 220.f, 24.f)) {
    cmd->step = true;
  }
  ui.SliderInt("Y 层", &edit.layer_y, 0, 63);
  ui.SliderInt("笔刷半径", &edit.brush_radius, 0, 4);
  ui.Checkbox("框选", &edit.box_mode);
  if (edit.box_has_a) {
    ui.Text("已设角点 A");
  }
  if (ui.Button("填充 Y 层", 220.f, 24.f)) {
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
  const std::string brush_text = std::string("笔刷: ") + mc::GetDef(edit.brush).name;
  ui.Text(brush_text.c_str());
  ui.EndWindow();
}

}  // namespace editor
