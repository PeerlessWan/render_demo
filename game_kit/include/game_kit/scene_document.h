#pragma once

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_kit {

class GameRuntime;

// PREFAB_SCHEMA v3 frozen. 1/2 still load; CaptureWorld writes 3.
struct SceneComponent {
  std::string type;
  std::string mesh;
  std::string material;
  std::string script;
  std::string extra_json;
  std::string fields_json;  // Script public fields object
  int kind = 0;
  float range = 8.f;
  float intensity = 1.f;
  bool active = false;
  float fovy = 1.04719755f;
  float hx = 0.5f;
  float hy = 0.5f;
  float hz = 0.5f;
  int gid = 0;
  std::string atlas;
  float color_r = 1.f;
  float color_g = 0.95f;
  float color_b = 0.85f;
};

struct SceneNode {
  std::string id;
  std::string name;
  std::string parent;  // empty = root
  engine::scene::Transform transform{};
  bool visible = true;
  std::string prefab_id;
  std::string script_path;  // v1 alias; v3 prefers Script component
  std::vector<SceneComponent> components;
  std::string extra_json;
  std::string override_json;
};

inline constexpr int kSceneFormatCurrent = 3;

struct SceneDocument {
  int format_version = 1;
  std::string host_api_hint;
  std::vector<SceneNode> nodes;
  std::string extensions_json;
};

engine::Status SaveSceneDocument(const SceneDocument& doc, const std::filesystem::path& path);
engine::Result<SceneDocument> LoadSceneDocument(const std::filesystem::path& path);

SceneDocument CaptureWorld(const engine::scene::World& world);
SceneDocument CaptureWorld(const engine::scene::World& world, const GameRuntime& rt);
engine::Status ApplyWorld(engine::scene::World& world, const SceneDocument& doc,
                          std::unordered_map<std::string, engine::scene::NodeId>* out_ids = nullptr);
[[nodiscard]] engine::Status ValidateSceneDocument(const SceneDocument& doc);
void BindSceneScripts(GameRuntime& rt, engine::scene::World& world, const SceneDocument& doc,
                      const std::unordered_map<std::string, engine::scene::NodeId>& ids);
void ClearWorld(engine::scene::World& world);

void ApplyNodeComponents(engine::scene::World& world, engine::scene::NodeId id,
                         const SceneNode& node);
void CaptureNodeComponents(const engine::scene::World& world, engine::scene::NodeId id,
                           SceneNode* node);

}  // namespace game_kit
