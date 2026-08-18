#pragma once

#include "game_kit/scene_document.h"

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <filesystem>
#include <string>

namespace game_kit {

struct PrefabDocument {
  int format_version = 1;
  std::string prefab_id;
  SceneDocument scene;
};

engine::Status SavePrefabDocument(const PrefabDocument& doc, const std::filesystem::path& path);
engine::Result<PrefabDocument> LoadPrefabDocument(const std::filesystem::path& path);

class GameRuntime;

// Deep-copy prefab nodes into world with new NodeIds. Applies world_trs to the new root.
// If rt is set, attaches ScriptComponents from script_path / Script components.
engine::scene::NodeId Instantiate(engine::scene::World& world, const PrefabDocument& prefab,
                                  const engine::scene::Transform& world_trs,
                                  GameRuntime* rt = nullptr);

PrefabDocument MakeChestTagPrefab();
PrefabDocument MakeTreePrefab();
PrefabDocument MakeHutPrefab();

}  // namespace game_kit
