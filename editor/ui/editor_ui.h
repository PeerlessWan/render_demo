#pragma once

#include "editing/selection.h"
#include "editing/settings.h"
#include "editing/undo.h"
#include "io/content_browser.h"
#include "play/scene_play.h"

#include "engine/scene/world.h"
#include "engine/ui/immediate_ui.h"

#include <filesystem>
#include <string>
#include <unordered_map>
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
  bool bake = false;
  bool lint = false;
  bool rescan = false;
  bool step = false;
  bool apply_prefab = false;
  bool revert_prefab = false;
  bool sculpt = false;
  bool tile_paint = false;
  int create_kind = -1;
  int place_prefab = -1;
  int place_content = -1;
  int open_scene = -1;
  std::string drop_parent;
  std::string drop_payload;
  int anim_state = -1;
};

void CollectNodes(const engine::scene::World& world, engine::scene::NodeId id,
                  std::vector<engine::scene::NodeId>* out);

void CollectNodesDeep(const engine::scene::World& world, engine::scene::NodeId id,
                      std::vector<engine::scene::NodeId>* out, std::vector<int>* depths, int depth);

void DrawEditorUi(engine::ui::ImmediateUi& ui, engine::scene::World& world, Selection& sel,
                  UndoStack& undo, const std::filesystem::path& scene_path, EditorSettings& settings,
                  EditorCommands* cmd, std::string* script_path, bool playing, bool paused,
                  ContentBrowser* content, const std::vector<std::string>& scripts, float fps,
                  bool multi_mod = false,
                  std::unordered_map<engine::scene::NodeId, NodeMeta>* meta = nullptr);

}  // namespace editor
