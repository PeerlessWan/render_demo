#include "editing/selection.h"
#include "editing/undo.h"
#include "io/scene_io.h"

#include "game_kit/scene_document.h"

#include "kit_test.h"

#include "engine/scene/world.h"

#include <filesystem>

TEST_CASE("selection default invalid", "[ed0]") {
  editor::Selection sel;
  REQUIRE(sel.node == engine::scene::kInvalidNode);
  REQUIRE(!sel.dragging);
}

TEST_CASE("undo stack transform roundtrip", "[ed1]") {
  engine::scene::World world;
  const auto node = world.CreateNode("cube");
  engine::scene::Transform before{};
  before.position = {0.f, 0.f, 0.f};
  world.set_local_transform(node, before);
  engine::scene::Transform after = before;
  after.position = {3.f, 0.f, 0.f};
  world.set_local_transform(node, after);

  editor::UndoStack undo;
  undo.Push(node, before, after);
  REQUIRE(undo.Undo(world));
  REQUIRE(world.local_transform(node).position.x == 0.f);
  REQUIRE(undo.Redo(world));
  REQUIRE(world.local_transform(node).position.x == 3.f);
}

TEST_CASE("scene_io save load roundtrip", "[ed2][ed3]") {
  engine::scene::World world;
  const auto cube = world.CreateNode("cube");
  engine::scene::Transform t;
  t.position = {1.f, 2.f, 3.f};
  world.set_local_transform(cube, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(cube, mesh);

  const auto path = std::filesystem::temp_directory_path() / "editor_smoke_scene.json";
  REQUIRE(editor::SaveScene(world, path).ok());

  engine::scene::World loaded;
  REQUIRE(editor::LoadScene(loaded, path).ok());
  REQUIRE(!loaded.roots().empty());
}

TEST_CASE("play enter exit restores snapshot without crash", "[ed4]") {
  engine::scene::World world;
  const auto cube = world.CreateNode("cube");
  engine::scene::Transform t;
  t.position = {0.f, 0.5f, 0.f};
  world.set_local_transform(cube, t);

  // Enter Play: snapshot (same-process policy, ADR 0001).
  const auto scene_snap = game_kit::CaptureWorld(world);
  REQUIRE(!scene_snap.nodes.empty());

  // Runtime dirties the scene.
  t.position = {9.f, 9.f, 9.f};
  world.set_local_transform(cube, t);
  (void)world.CreateNode("runtime_spawn");

  // Exit Play: restore snapshot.
  game_kit::ClearWorld(world);
  REQUIRE(game_kit::ApplyWorld(world, scene_snap).ok());
  REQUIRE(!world.roots().empty());
  bool found = false;
  for (const auto root : world.roots()) {
    if (world.name(root) == "cube") {
      found = true;
      REQUIRE(world.local_transform(root).position.x == 0.f);
    }
  }
  REQUIRE(found);
}

TEST_CASE("script path field survives scene document", "[ed5]") {
  game_kit::SceneDocument doc;
  game_kit::SceneNode n;
  n.id = "1";
  n.name = "prop";
  n.script_path = "scripts/chest.lua";
  n.prefab_id = "chest_tag";
  doc.nodes.push_back(n);
  const auto path = std::filesystem::temp_directory_path() / "editor_smoke_script_field.json";
  REQUIRE(game_kit::SaveSceneDocument(doc, path).ok());
  auto loaded = game_kit::LoadSceneDocument(path);
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().nodes[0].script_path == "scripts/chest.lua");
  REQUIRE(loaded.value().nodes[0].prefab_id == "chest_tag");
}
