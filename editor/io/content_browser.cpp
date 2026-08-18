#include "io/content_browser.h"

#include "engine/assets/manifest.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

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
    c.material = mesh->material_id;
    n.components.push_back(std::move(c));
  }
  if (const auto* L = world.light(id)) {
    game_kit::SceneComponent c;
    c.type = "Light";
    c.kind = L->kind;
    c.range = L->range;
    c.intensity = L->intensity;
    n.components.push_back(std::move(c));
  }
  if (const auto* cam = world.camera(id)) {
    game_kit::SceneComponent c;
    c.type = "Camera";
    c.active = cam->active;
    c.fovy = cam->fovy_rad;
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
      if (!ent.is_regular_file(ec)) {
        continue;
      }
      ContentItem it;
      it.path = ent.path();
      it.label = ent.path().stem().string();
      const auto ext = ent.path().extension().string();
      if (ext == ".json") {
        it.kind = LooksLikePrefab(ent.path()) ? ContentItem::Kind::Prefab : ContentItem::Kind::Scene;
        it.type = it.kind == ContentItem::Kind::Prefab ? "prefab" : "scene";
        it.thumb_r = it.kind == ContentItem::Kind::Prefab ? 0.85f : 0.35f;
        it.thumb_g = it.kind == ContentItem::Kind::Prefab ? 0.55f : 0.7f;
        it.thumb_b = 0.35f;
      } else if (ext == ".lua") {
        it.kind = ContentItem::Kind::Script;
        it.type = "script";
        it.thumb_r = 0.4f;
        it.thumb_g = 0.8f;
        it.thumb_b = 0.45f;
      } else {
        continue;
      }
      it.asset_id = "asset:" + it.label;
      items.push_back(std::move(it));
    }
    const auto manifest_path = root / "manifest.json";
    if (std::filesystem::exists(manifest_path, ec)) {
      auto man = engine::assets::Manifest::LoadFromFile(manifest_path);
      if (man) {
        for (const auto& [id, entry] : man.value().entries()) {
          bool found = false;
          for (auto& it : items) {
            if (it.label == id.value() || it.path.string() == entry.path) {
              it.asset_id = id.value();
              it.type = entry.type;
              found = true;
              break;
            }
          }
          if (!found) {
            ContentItem it;
            it.asset_id = id.value();
            it.label = id.value();
            it.path = entry.path;
            it.type = entry.type;
            it.kind = entry.type == "prefab" ? ContentItem::Kind::Prefab
                                            : (entry.type == "scene" ? ContentItem::Kind::Scene
                                                                     : ContentItem::Kind::Mesh);
            it.thumb_r = 0.7f;
            it.thumb_g = 0.7f;
            it.thumb_b = 0.3f;
            items.push_back(std::move(it));
          }
        }
      }
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
  return CaptureSelectionPrefab(world, node, nullptr);
}

game_kit::PrefabDocument CaptureSelectionPrefab(
    const engine::scene::World& world, engine::scene::NodeId node,
    const std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  game_kit::PrefabDocument p;
  if (!world.valid(node)) {
    return p;
  }
  p.format_version = game_kit::kSceneFormatCurrent;
  p.prefab_id = world.name(node).empty() ? "selection" : world.name(node);
  WalkPrefab(world, node, {}, p.scene);
  p.scene.format_version = game_kit::kSceneFormatCurrent;
  if (!p.scene.nodes.empty()) {
    p.scene.nodes[0].prefab_id = p.prefab_id;
  }
  if (meta) {
    StampMeta(&p.scene, *meta);
  }
  return p;
}

engine::Status SaveSelectionPrefab(const engine::scene::World& world, engine::scene::NodeId node,
                                   const std::filesystem::path& path) {
  return SaveSelectionPrefab(world, node, path, nullptr);
}

engine::Status SaveSelectionPrefab(const engine::scene::World& world, engine::scene::NodeId node,
                                   const std::filesystem::path& path,
                                   const std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  auto doc = CaptureSelectionPrefab(world, node, meta);
  if (doc.scene.nodes.empty()) {
    return engine::Status::Fail("no selection");
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  return game_kit::SavePrefabDocument(doc, path);
}

bool TryRunTool(std::string_view exe_stem) {
  const std::string name = std::string(exe_stem) + ".exe";
  std::vector<std::filesystem::path> candidates = {
      std::filesystem::current_path() / name,
      std::filesystem::current_path() / "Release" / name,
      std::filesystem::path("render_demo") / "build_kits" / "Release" / name,
      std::filesystem::path("render_demo") / "build_kits" / "render_engine" / "tools" / "content_lint" /
          "Release" / name,
      std::filesystem::path("render_demo") / "build_kits" / "render_engine" / "tools" / "lightmap_baker" /
          "Release" / name,
      std::filesystem::path("render_demo") / "build_kits" / "render_engine" / "tools" / "asset_cook" /
          "Release" / name,
  };
  char mod[MAX_PATH]{};
  if (GetModuleFileNameA(nullptr, mod, MAX_PATH) > 0) {
    const auto dir = std::filesystem::path(mod).parent_path();
    candidates.insert(candidates.begin(), dir / name);
    candidates.insert(candidates.begin() + 1, dir.parent_path() / "Release" / name);
  }
  for (const auto& p : candidates) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
      continue;
    }
    const std::string cmd = "\"" + p.string() + "\"";
    engine::LogInfo(std::string(exe_stem) + ": " + p.string());
    (void)std::system(cmd.c_str());
    return true;
  }
  engine::LogInfo(std::string(exe_stem) + " not found; skip");
  return false;
}

bool TryRunAssetCook() { return TryRunTool("asset_cook"); }

}  // namespace editor
