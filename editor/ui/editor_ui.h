#pragma once

#include "editing/selection.h"
#include "editing/settings.h"
#include "editing/undo.h"
#include "io/content_browser.h"

#include "engine/scene/world.h"
#include "engine/ui/immediate_ui.h"

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

struct EditorCommands {
  bool save = false;
  bool load = false;
  bool play = false;
  bool pause = false;
  bool duplicate = false;
  bool destroy = false;
  bool undo = false;
  bool redo = false;
  bool frame = false;
  bool save_prefab = false;
  bool cook = false;
  bool rescan = false;
  int place_prefab = -1;
  int place_content = -1;
};

void CollectNodes(const engine::scene::World& world, engine::scene::NodeId id,
                  std::vector<engine::scene::NodeId>* out);

void CollectNodesDeep(const engine::scene::World& world, engine::scene::NodeId id,
                      std::vector<engine::scene::NodeId>* out, std::vector<int>* depths, int depth);

void DrawEditorUi(engine::ui::ImmediateUi& ui, engine::scene::World& world, Selection& sel,
                  UndoStack& undo, const std::filesystem::path& scene_path, EditorSettings& settings,
                  EditorCommands* cmd, std::string* script_path, bool playing, bool paused,
                  ContentBrowser* content, const std::vector<std::string>& scripts, float fps);

}  // namespace editor
