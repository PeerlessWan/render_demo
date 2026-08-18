#pragma once

#include "game_kit/prefab.h"
#include "play/scene_play.h"

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

struct ContentItem {
  enum class Kind { Scene, Prefab, Mesh, Script, Other } kind = Kind::Prefab;
  std::filesystem::path path;
  std::string label;
  std::string asset_id;
  std::string type;
  float thumb_r = 0.35f;
  float thumb_g = 0.55f;
  float thumb_b = 0.85f;
  int thumb_w = 0;
  int thumb_h = 0;
  std::vector<std::uint8_t> thumb_px;
};

struct ContentBrowser {
  std::vector<ContentItem> items;
  int pending = -1;

  void Scan(const std::vector<std::filesystem::path>& roots);
};

void ScanLuaScripts(const std::vector<std::filesystem::path>& roots, std::vector<std::string>* out);

game_kit::PrefabDocument CaptureSelectionPrefab(const engine::scene::World& world,
                                                engine::scene::NodeId node);
game_kit::PrefabDocument CaptureSelectionPrefab(
    const engine::scene::World& world, engine::scene::NodeId node,
    const std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

engine::Status SaveSelectionPrefab(const engine::scene::World& world, engine::scene::NodeId node,
                                   const std::filesystem::path& path);
engine::Status SaveSelectionPrefab(const engine::scene::World& world, engine::scene::NodeId node,
                                   const std::filesystem::path& path,
                                   const std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

bool TryRunAssetCook();
bool TryRunTool(std::string_view exe_stem);

}  // namespace editor
