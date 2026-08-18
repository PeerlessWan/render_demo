#pragma once

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_kit {

// PREFAB_SCHEMA v1 scene document (editor + runtime share this).
struct SceneComponent {
  std::string type;
  std::string mesh;
  std::string script;
  std::string extra_json;
};

struct SceneNode {
  std::string id;
  std::string name;
  std::string parent;  // empty = root
  engine::scene::Transform transform{};
  bool visible = true;
  std::string prefab_id;
  std::string script_path;
  std::vector<SceneComponent> components;
};

struct SceneDocument {
  int format_version = 1;
  std::vector<SceneNode> nodes;
  std::string extensions_json;
};

engine::Status SaveSceneDocument(const SceneDocument& doc, const std::filesystem::path& path);
engine::Result<SceneDocument> LoadSceneDocument(const std::filesystem::path& path);

SceneDocument CaptureWorld(const engine::scene::World& world);
engine::Status ApplyWorld(engine::scene::World& world, const SceneDocument& doc,
                          std::unordered_map<std::string, engine::scene::NodeId>* out_ids = nullptr);
void ClearWorld(engine::scene::World& world);

}  // namespace game_kit
