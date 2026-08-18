#include "game_kit/prefab.h"

#include "game_kit/runtime.h"

#include <unordered_map>

namespace game_kit {
namespace {

void AddMesh(SceneNode* n, std::string mesh, engine::Vec3 pos, engine::Vec3 scale) {
  if (!n) {
    return;
  }
  n->transform.position = pos;
  n->transform.scale = scale;
  SceneComponent c;
  c.type = "MeshRenderer";
  c.mesh = std::move(mesh);
  n->components.push_back(std::move(c));
}

}  // namespace

engine::Status SavePrefabDocument(const PrefabDocument& doc, const std::filesystem::path& path) {
  SceneDocument scene = doc.scene;
  scene.format_version = doc.format_version;
  if (scene.extensions_json.empty()) {
    scene.extensions_json = std::string("{\"game_kit\":{\"prefab_id\":\"") + doc.prefab_id + "\"}}";
  }
  if (!scene.nodes.empty() && scene.nodes[0].prefab_id.empty()) {
    scene.nodes[0].prefab_id = doc.prefab_id;
  }
  return SaveSceneDocument(scene, path);
}

engine::Result<PrefabDocument> LoadPrefabDocument(const std::filesystem::path& path) {
  auto scene = LoadSceneDocument(path);
  if (!scene) {
    return engine::Result<PrefabDocument>::Fail(scene.status().message());
  }
  PrefabDocument doc;
  doc.format_version = scene.value().format_version;
  doc.scene = std::move(scene.value());
  if (!doc.scene.nodes.empty()) {
    doc.prefab_id = doc.scene.nodes[0].prefab_id;
  }
  if (doc.prefab_id.empty()) {
    doc.prefab_id = path.stem().string();
  }
  return engine::Result<PrefabDocument>::Ok(std::move(doc));
}

engine::scene::NodeId Instantiate(engine::scene::World& world, const PrefabDocument& prefab,
                                  const engine::scene::Transform& world_trs, GameRuntime* rt) {
  std::unordered_map<std::string, engine::scene::NodeId> ids;
  if (auto st = ApplyWorld(world, prefab.scene, &ids); !st) {
    return engine::scene::kInvalidNode;
  }
  engine::scene::NodeId root = engine::scene::kInvalidNode;
  for (const auto& n : prefab.scene.nodes) {
    if (!n.parent.empty()) {
      continue;
    }
    auto it = ids.find(n.id);
    if (it != ids.end()) {
      root = it->second;
      break;
    }
  }
  if (root != engine::scene::kInvalidNode) {
    world.set_local_transform(root, world_trs);
    if (!prefab.prefab_id.empty()) {
      world.set_name(root, prefab.prefab_id);
    }
  }
  if (!rt || root == engine::scene::kInvalidNode) {
    return root;
  }

  rt->set_world(&world);
  rt->scripts().AttachHost(&world, rt);
  const auto eid = rt->entities().Create(
      prefab.prefab_id.empty() ? world.name(root) : prefab.prefab_id, root);

  for (const auto& n : prefab.scene.nodes) {
    std::string path = n.script_path;
    for (const auto& c : n.components) {
      if (c.type == "Script" && !c.script.empty()) {
        path = c.script;
      }
    }
    if (path.empty()) {
      continue;
    }
    auto it = ids.find(n.id);
    if (it == ids.end()) {
      continue;
    }
    const auto nid = it->second;
    const auto sid = rt->scripts().Attach(nid, rt->ResolveScriptPath(path).string());
    if (auto* sc = rt->scripts().Get(sid)) {
      sc->entity = (nid == root) ? eid : kInvalidEntity;
      (void)rt->scripts().LoadFromDisk(*sc);
    }
  }
  return root;
}

PrefabDocument MakeChestTagPrefab() {
  PrefabDocument p;
  p.prefab_id = "chest_tag";
  SceneNode chest;
  chest.id = "root";
  chest.name = "chest_tag";
  chest.prefab_id = "chest_tag";
  chest.script_path = "scripts/chest.lua";
  AddMesh(&chest, "cube", {0.f, 0.5f, 0.f}, {1.f, 1.f, 1.f});
  SceneComponent script;
  script.type = "Script";
  script.script = "scripts/chest.lua";
  chest.components.push_back(std::move(script));
  SceneNode tag;
  tag.id = "tag";
  tag.name = "label";
  tag.parent = "root";
  AddMesh(&tag, "cube", {0.f, 1.15f, 0.f}, {0.45f, 0.12f, 0.45f});
  p.scene.nodes.push_back(std::move(chest));
  p.scene.nodes.push_back(std::move(tag));
  return p;
}

PrefabDocument MakeTreePrefab() {
  PrefabDocument p;
  p.prefab_id = "tree";
  SceneNode trunk;
  trunk.id = "root";
  trunk.name = "tree";
  trunk.prefab_id = "tree";
  AddMesh(&trunk, "cube", {0.f, 1.f, 0.f}, {0.4f, 2.f, 0.4f});
  SceneNode leaves;
  leaves.id = "leaves";
  leaves.name = "leaves";
  leaves.parent = "root";
  AddMesh(&leaves, "cube", {0.f, 2.4f, 0.f}, {2.2f, 1.6f, 2.2f});
  p.scene.nodes.push_back(std::move(trunk));
  p.scene.nodes.push_back(std::move(leaves));
  return p;
}

PrefabDocument MakeHutPrefab() {
  PrefabDocument p;
  p.prefab_id = "hut";
  SceneNode floor;
  floor.id = "root";
  floor.name = "hut";
  floor.prefab_id = "hut";
  AddMesh(&floor, "cube", {0.f, 0.1f, 0.f}, {4.f, 0.2f, 4.f});
  SceneNode wall;
  wall.id = "wall";
  wall.name = "wall";
  wall.parent = "root";
  AddMesh(&wall, "cube", {0.f, 1.2f, -1.8f}, {4.f, 2.2f, 0.2f});
  SceneNode roof;
  roof.id = "roof";
  roof.name = "roof";
  roof.parent = "root";
  AddMesh(&roof, "cube", {0.f, 2.5f, 0.f}, {4.4f, 0.25f, 4.4f});
  p.scene.nodes.push_back(std::move(floor));
  p.scene.nodes.push_back(std::move(wall));
  p.scene.nodes.push_back(std::move(roof));
  return p;
}

}  // namespace game_kit
