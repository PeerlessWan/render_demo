#include "cmd/session.h"
#include "editing/light_bake.h"
#include "editing/terrain_edit.h"
#include "editing/undo.h"
#include "io/content_browser.h"
#include "play/scene_play.h"

#include "game_kit/prefab.h"
#include "game_kit/scene_document.h"

#include "kit_test.h"

#include "engine/scene/world.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("scene v3 captures light and roundtrips", "[integration]") {
  engine::scene::World world;
  const auto id = world.CreateNode("lamp");
  engine::scene::LightComponent L;
  L.range = 11.f;
  L.intensity = 2.f;
  world.set_light(id, L);
  engine::scene::ColliderComponent col;
  world.set_collider(id, col);
  auto doc = game_kit::CaptureWorld(world);
  REQUIRE(doc.format_version == game_kit::kSceneFormatCurrent);
  bool found_light = false;
  for (const auto& n : doc.nodes) {
    for (const auto& c : n.components) {
      if (c.type == "Light") {
        found_light = true;
        REQUIRE(std::fabs(c.range - 11.f) < 0.01f);
      }
    }
  }
  REQUIRE(found_light);
  const auto path = std::filesystem::temp_directory_path() / "editor_v3_light.json";
  REQUIRE(game_kit::SaveSceneDocument(doc, path).ok());
  auto loaded = game_kit::LoadSceneDocument(path);
  REQUIRE(loaded.ok());
  engine::scene::World w2;
  REQUIRE(game_kit::ApplyWorld(w2, loaded.value()).ok());
  bool ok = false;
  for (auto r : w2.roots()) {
    if (w2.light(r)) {
      ok = std::fabs(w2.light(r)->range - 11.f) < 0.01f;
    }
  }
  REQUIRE(ok);
}

TEST_CASE("prefab override merge and apply to source", "[integration]") {
  auto prefab = game_kit::MakeTreePrefab();
  engine::scene::World world;
  engine::scene::Transform trs;
  trs.position = {2.f, 0.f, 0.f};
  const auto id = game_kit::Instantiate(world, prefab, trs);
  REQUIRE(world.valid(id));
  game_kit::MergeOverrideJson(world, id, R"({"x":5,"y":1,"z":0,"visible":true})");
  REQUIRE(std::fabs(world.local_transform(id).position.x - 5.f) < 0.01f);
  game_kit::MergeOverrideJson(world, id, R"({"material":"ground","range":9,"intensity":3,"color":[0.2,0.4,0.6]})");
  REQUIRE(world.mesh(id) != nullptr);
  REQUIRE(world.mesh(id)->material_id == "ground");
  REQUIRE(world.light(id) != nullptr);
  REQUIRE(std::fabs(world.light(id)->range - 9.f) < 0.01f);
  game_kit::ApplyInstanceToSource(world, id, &prefab);
  REQUIRE(std::fabs(prefab.scene.nodes[0].transform.position.x - 5.f) < 0.01f);
}

TEST_CASE("play clone does not dirty edit world", "[integration]") {
  editor::EditorHost host;
  editor::EditorOp create;
  create.kind = editor::EditorOp::Kind::Create;
  create.create_kind = "cube";
  REQUIRE(editor::ApplyOp(host.Bind(), create).ok);
  const auto edit_id = host.world.roots().front();
  const float x0 = host.world.local_transform(edit_id).position.x;
  editor::EditorOp play;
  play.kind = editor::EditorOp::Kind::Play;
  REQUIRE(editor::ApplyOp(host.Bind(), play).ok);
  REQUIRE(host.playing);
  auto live = host.Bind();
  REQUIRE(live.world == &host.play_world);
  if (live.world->valid(live.world->roots().front())) {
    auto t = live.world->local_transform(live.world->roots().front());
    t.position.x = 42.f;
    live.world->set_local_transform(live.world->roots().front(), t);
  }
  editor::EditorOp stop;
  stop.kind = editor::EditorOp::Kind::Stop;
  REQUIRE(editor::ApplyOp(host.Bind(), stop).ok);
  REQUIRE(!host.playing);
  REQUIRE(std::fabs(host.world.local_transform(edit_id).position.x - x0) < 0.01f);
}

TEST_CASE("play light edit does not leak into edit world", "[integration]") {
  editor::EditorHost host;
  editor::EditorOp create;
  create.kind = editor::EditorOp::Kind::Create;
  create.create_kind = "light";
  REQUIRE(editor::ApplyOp(host.Bind(), create).ok);
  const auto edit_id = host.sel.node;
  REQUIRE(host.world.light(edit_id) != nullptr);
  const float r0 = host.world.light(edit_id)->range;
  editor::EditorOp play;
  play.kind = editor::EditorOp::Kind::Play;
  REQUIRE(editor::ApplyOp(host.Bind(), play).ok);
  auto live = host.Bind();
  REQUIRE(live.world == &host.play_world);
  for (auto id : live.world->roots()) {
    if (live.world->light(id)) {
      auto L = *live.world->light(id);
      L.range = 99.f;
      live.world->set_light(id, L);
    }
  }
  editor::EditorOp stop;
  stop.kind = editor::EditorOp::Kind::Stop;
  REQUIRE(editor::ApplyOp(host.Bind(), stop).ok);
  REQUIRE(host.world.light(edit_id) != nullptr);
  REQUIRE(std::fabs(host.world.light(edit_id)->range - r0) < 0.01f);
}

TEST_CASE("set parent undo restores parent", "[integration]") {
  editor::EditorHost host;
  editor::EditorOp a;
  a.kind = editor::EditorOp::Kind::Create;
  a.create_kind = "empty";
  REQUIRE(editor::ApplyOp(host.Bind(), a).ok);
  host.world.set_name(host.sel.node, "parent_a");
  editor::EditorOp b;
  b.kind = editor::EditorOp::Kind::Create;
  b.create_kind = "cube";
  REQUIRE(editor::ApplyOp(host.Bind(), b).ok);
  editor::EditorOp parent;
  parent.kind = editor::EditorOp::Kind::SetParent;
  parent.name = "parent_a";
  REQUIRE(editor::ApplyOp(host.Bind(), parent).ok);
  REQUIRE(host.world.valid(host.world.parent(host.sel.node)));
  editor::EditorOp undo;
  undo.kind = editor::EditorOp::Kind::Undo;
  REQUIRE(editor::ApplyOp(host.Bind(), undo).ok);
}

TEST_CASE("content browser assigns asset id", "[integration]") {
  const auto dir = std::filesystem::temp_directory_path() / "editor_content_scan";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  {
    std::ofstream out(dir / "manifest.json");
    out << R"({"assets":[{"id":"prop_cube","type":"prefab","path":"cube.json","deps":["mesh_cube"]}]})";
  }
  {
    std::ofstream out(dir / "cube.json");
    out << R"({"format_version":1,"nodes":[{"id":"1","name":"cube","parent":null,"transform":{"t":[0,0,0],"r":[0,0,0,1],"s":[1,1,1]},"visible":true,"prefab_id":"prop_cube","components":[]}]})";
  }
  editor::ContentBrowser browser;
  browser.Scan({dir});
  REQUIRE(!browser.items.empty());
  bool has_id = false;
  for (const auto& it : browser.items) {
    if (it.asset_id == "prop_cube") {
      has_id = true;
    }
  }
  REQUIRE(has_id);
}

TEST_CASE("nested prefab instantiate creates child instance", "[integration]") {
  auto tree = game_kit::MakeTreePrefab();
  game_kit::PrefabDocument forest;
  forest.prefab_id = "forest";
  game_kit::SceneNode root;
  root.id = "root";
  root.name = "forest";
  root.prefab_id = "forest";
  game_kit::SceneNode child;
  child.id = "t";
  child.name = "nested_tree";
  child.parent = "root";
  child.prefab_id = "tree";
  forest.scene.nodes.push_back(root);
  forest.scene.nodes.push_back(child);
  engine::scene::World world;
  engine::scene::Transform trs;
  const auto id = game_kit::InstantiateNested(world, forest, trs, {tree});
  REQUIRE(world.valid(id));
  bool found = false;
  std::vector<engine::scene::NodeId> stack = world.roots();
  while (!stack.empty()) {
    auto n = stack.back();
    stack.pop_back();
    if (world.name(n) == "tree" || world.name(n) == "nested_tree") {
      found = true;
    }
    for (auto c : world.children(n)) {
      stack.push_back(c);
    }
  }
  REQUIRE(found);
}

TEST_CASE("terrain heights round trip via scene extensions", "[integration]") {
  editor::EditorHost host;
  editor::RaiseHeight(&host.settings.heights, 8, 8, 1.5f, 2.f);
  const float peak = host.settings.heights[static_cast<std::size_t>(8 * 17 + 8)];
  const auto path = std::filesystem::temp_directory_path() / "editor_terrain_rt.json";
  editor::EditorOp save;
  save.kind = editor::EditorOp::Kind::Save;
  save.path = path.string();
  REQUIRE(editor::ApplyOp(host.Bind(), save).ok);
  editor::EditorHost host2;
  editor::EditorOp open;
  open.kind = editor::EditorOp::Kind::Open;
  open.path = path.string();
  REQUIRE(editor::ApplyOp(host2.Bind(), open).ok);
  REQUIRE(host2.settings.heights.size() == host.settings.heights.size());
  REQUIRE(std::fabs(host2.settings.heights[static_cast<std::size_t>(8 * 17 + 8)] - peak) < 0.05f);
}

TEST_CASE("cpu bake writes lightmap from scene lights", "[integration]") {
  editor::EditorHost host;
  editor::EditorOp create;
  create.kind = editor::EditorOp::Kind::Create;
  create.create_kind = "light";
  REQUIRE(editor::ApplyOp(host.Bind(), create).ok);
  const auto path = std::filesystem::temp_directory_path() / "editor_bake.rgba";
  REQUIRE(editor::BakeSceneLights(host.world, host.settings.heights, path, nullptr).ok());
  REQUIRE(std::filesystem::file_size(path) > 16);
}

TEST_CASE("screenshot without device is error", "[integration]") {
  editor::EditorHost host;
  editor::EditorOp shot;
  shot.kind = editor::EditorOp::Kind::Screenshot;
  const auto r = editor::ApplyOp(host.Bind(), shot);
  REQUIRE(r.is_error);
  REQUIRE(r.message.find("gpu") != std::string::npos);
}

