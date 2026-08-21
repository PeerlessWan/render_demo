#pragma once

#include "editing/selection.h"
#include "editing/settings.h"
#include "editing/undo.h"
#include "io/content_browser.h"
#include "play/scene_play.h"

#include "game_kit/runtime.h"
#include "game_kit/scene_document.h"
#include "game_kit/script_component.h"

#include "engine/scene/world.h"

namespace engine::rhi {
class IDevice;
}
namespace engine::render2d {
class TilemapStreamer;
}

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

struct EditorOp {
  enum class Kind {
    Dump,
    Open,
    Save,
    Create,
    Select,
    SetTransform,
    SetMesh,
    SetVisible,
    SetScript,
    SetName,
    SetParent,
    SetFields,
    Duplicate,
    Destroy,
    Undo,
    Redo,
    Play,
    Pause,
    Step,
    Stop,
    ListContent,
    HotReload,
    Bake,
    BakeNav,
    Lint,
    Place,
    ApplyPrefab,
    RevertPrefab,
    Screenshot,
    Sculpt,
    PaintTile
  } kind = Kind::Dump;

  std::string create_kind;
  std::string name;
  std::string path;
  std::string mesh;
  std::string script;
  bool add = false;
  bool visible = true;
  bool has_visible = false;
  bool has_x = false;
  bool has_y = false;
  bool has_z = false;
  bool has_yaw = false;
  bool has_pitch = false;
  bool has_roll = false;
  bool has_sx = false;
  bool has_sy = false;
  bool has_sz = false;
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float yaw = 0.f;
  float pitch = 0.f;
  float roll = 0.f;
  float sx = 1.f;
  float sy = 1.f;
  float sz = 1.f;
};

struct OpResult {
  bool ok = true;
  bool is_error = false;
  std::string message;
  std::string json;
};

struct EditorSession {
  engine::scene::World* world = nullptr;
  Selection* sel = nullptr;
  UndoStack* undo = nullptr;
  EditorSettings* settings = nullptr;
  std::unordered_map<engine::scene::NodeId, NodeMeta>* meta = nullptr;
  std::filesystem::path* scene_path = nullptr;
  bool* playing = nullptr;
  bool* paused = nullptr;
  game_kit::GameRuntime* rt = nullptr;
  game_kit::ScriptComponentWorld* scripts = nullptr;
  game_kit::SceneDocument* scene_snap = nullptr;
  ContentBrowser* content = nullptr;
  engine::scene::World* edit_world = nullptr;
  engine::scene::World* play_world = nullptr;
  std::string* lint_json = nullptr;
  std::string* screenshot_path = nullptr;
  engine::rhi::IDevice* device = nullptr;
  engine::render2d::TilemapStreamer* tiles = nullptr;
};

struct EditorHost {
  engine::scene::World world;
  Selection sel;
  UndoStack undo;
  EditorSettings settings;
  std::unordered_map<engine::scene::NodeId, NodeMeta> meta;
  std::filesystem::path scene_path{"editor_scene.json"};
  bool playing = false;
  bool paused = false;
  game_kit::GameRuntime rt;
  game_kit::ScriptComponentWorld scripts;
  game_kit::SceneDocument scene_snap;
  ContentBrowser content;
  engine::scene::World play_world;
  std::string lint_json;
  std::string screenshot_path;

  EditorSession Bind();
};

[[nodiscard]] std::string DumpSessionJson(const EditorSession& s);

OpResult ApplyOp(EditorSession s, const EditorOp& op);

engine::scene::NodeId CreatePrimitive(engine::scene::World& world, std::string_view kind,
                                      const EditorSettings& settings);

}  // namespace editor
