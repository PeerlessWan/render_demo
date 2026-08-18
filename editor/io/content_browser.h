#pragma once

#include "game_kit/prefab.h"

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

struct ContentItem {
  enum class Kind { Scene, Prefab } kind = Kind::Prefab;
  std::filesystem::path path;
  std::string label;
};

struct ContentBrowser {
  std::vector<ContentItem> items;
  int pending = -1;

  void Scan(const std::vector<std::filesystem::path>& roots);
};

void ScanLuaScripts(const std::vector<std::filesystem::path>& roots, std::vector<std::string>* out);

game_kit::PrefabDocument CaptureSelectionPrefab(const engine::scene::World& world,
                                                engine::scene::NodeId node);

engine::Status SaveSelectionPrefab(const engine::scene::World& world, engine::scene::NodeId node,
                                   const std::filesystem::path& path);

bool TryRunAssetCook();

}  // namespace editor
