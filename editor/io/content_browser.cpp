#include "io/content_browser.h"

#include "game_kit/scene_document.h"

#include "engine/core/log.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace editor {
namespace {

void WalkPrefab(const engine::scene::World& world, engine::scene::NodeId id,
                const std::string& parent, game_kit::SceneDocument& doc) {
  if (!world.valid(id)) {
    return;
  }
  game_kit::SceneNode n;
  n.id = std::to_string(id);
  n.name = world.name(id);
  n.parent = parent;
  n.transform = world.local_transform(id);
  n.visible = world.visible(id);
  if (const auto* mesh = world.mesh(id)) {
    game_kit::SceneComponent c;
    c.type = "MeshRenderer";
    c.mesh = mesh->mesh_id;
    n.components.push_back(std::move(c));
  }
  doc.nodes.push_back(std::move(n));
  const std::string self = doc.nodes.back().id;
  for (auto c : world.children(id)) {
    WalkPrefab(world, c, self, doc);
  }
}

bool LooksLikePrefab(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return t.find("prefab_id") != std::string::npos;
}

}  // namespace

void ContentBrowser::Scan(const std::vector<std::filesystem::path>& roots) {
  items.clear();
  pending = -1;
  std::error_code ec;
  for (const auto& root : roots) {
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
      continue;
    }
    for (const auto& ent : std::filesystem::directory_iterator(root, ec)) {
      if (!ent.is_regular_file(ec) || ent.path().extension() != ".json") {
        continue;
      }
      ContentItem it;
      it.path = ent.path();
      it.label = ent.path().stem().string();
      it.kind = LooksLikePrefab(ent.path()) ? ContentItem::Kind::Prefab : ContentItem::Kind::Scene;
      items.push_back(std::move(it));
    }
  }
  std::sort(items.begin(), items.end(),
            [](const ContentItem& a, const ContentItem& b) { return a.label < b.label; });
}

void ScanLuaScripts(const std::vector<std::filesystem::path>& roots, std::vector<std::string>* out) {
  if (!out) {
    return;
  }
  out->clear();
  std::error_code ec;
  for (const auto& root : roots) {
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
      continue;
    }
    for (const auto& ent : std::filesystem::directory_iterator(root, ec)) {
      if (!ent.is_regular_file(ec) || ent.path().extension() != ".lua") {
        continue;
      }
      out->push_back(ent.path().generic_string());
    }
  }
  std::sort(out->begin(), out->end());
}

game_kit::PrefabDocument CaptureSelectionPrefab(const engine::scene::World& world,
                                                engine::scene::NodeId node) {
  game_kit::PrefabDocument p;
  if (!world.valid(node)) {
    return p;
  }
  p.prefab_id = world.name(node).empty() ? "selection" : world.name(node);
  WalkPrefab(world, node, {}, p.scene);
  if (!p.scene.nodes.empty()) {
    p.scene.nodes[0].prefab_id = p.prefab_id;
  }
  return p;
}

engine::Status SaveSelectionPrefab(const engine::scene::World& world, engine::scene::NodeId node,
                                   const std::filesystem::path& path) {
  auto doc = CaptureSelectionPrefab(world, node);
  if (doc.scene.nodes.empty()) {
    return engine::Status::Fail("no selection");
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  return game_kit::SavePrefabDocument(doc, path);
}

bool TryRunAssetCook() {
  const std::filesystem::path candidates[] = {
      std::filesystem::current_path() / "asset_cook.exe",
      std::filesystem::current_path() / "Release" / "asset_cook.exe",
      std::filesystem::current_path() / "RelWithDebInfo" / "asset_cook.exe",
      std::filesystem::path("render_demo") / "build_kits" / "Release" / "asset_cook.exe",
  };
  for (const auto& p : candidates) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
      continue;
    }
    const std::string cmd = "\"" + p.string() + "\"";
    engine::LogInfo("cook: " + p.string());
    (void)std::system(cmd.c_str());
    return true;
  }
  engine::LogInfo("asset_cook not found; skip cook");
  return false;
}

}  // namespace editor
