#pragma once

#include "engine/scene/world.h"
#include "engine/ui/immediate_ui.h"

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

struct Selection;
class UndoStack;

void CollectNodes(const engine::scene::World& world, engine::scene::NodeId id,
                  std::vector<engine::scene::NodeId>* out);

void DrawEditorUi(engine::ui::ImmediateUi& ui, engine::scene::World& world, Selection& sel,
                  UndoStack& undo, const std::filesystem::path& scene_path, bool* save_clicked,
                  bool* load_clicked, bool* play_clicked, bool playing, std::string* script_path,
                  int* place_prefab);

}  // namespace editor
