#include "editing/ops.h"
#include "editing/selection.h"
#include "editing/snap.h"
#include "editing/undo.h"
#include "editing/gizmo.h"
#include "editing/ray.h"
#include "io/content_browser.h"
#include "io/scene_io.h"
#include "play/scene_play.h"
#include "cmd/session.h"
#include "mcp/protocol.h"

#include "game_kit/prefab.h"
#include "game_kit/runtime.h"
#include "game_kit/scene_document.h"

#include "kit_test.h"

#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

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

TEST_CASE("snap rounds to grid", "[ed6]") {
  REQUIRE(editor::SnapScalar(1.4f, 1.f) == 1.f);
  REQUIRE(editor::SnapScalar(1.6f, 1.f) == 2.f);
  const auto p = editor::SnapVec3({1.6f, 0.4f, -1.6f}, 1.f);
  REQUIRE(p.x == 2.f);
  REQUIRE(p.y == 0.f);
  REQUIRE(p.z == -2.f);
}

TEST_CASE("selection toggle multi", "[ed6]") {
  engine::scene::World world;
  const auto a = world.CreateNode("a");
  const auto b = world.CreateNode("b");
  editor::Selection sel;
  sel.Set(a);
  sel.Toggle(b);
  REQUIRE(sel.Contains(a));
  REQUIRE(sel.Contains(b));
  REQUIRE(sel.All().size() == 2);
  sel.Toggle(a);
  REQUIRE(!sel.Contains(a));
  REQUIRE(sel.Contains(b));
}

TEST_CASE("batch undo moves two nodes", "[ed6]") {
  engine::scene::World world;
  const auto a = world.CreateNode("a");
  const auto b = world.CreateNode("b");
  engine::scene::Transform ta;
  ta.position = {0.f, 0.f, 0.f};
  engine::scene::Transform tb;
  tb.position = {1.f, 0.f, 0.f};
  world.set_local_transform(a, ta);
  world.set_local_transform(b, tb);
  engine::scene::Transform ta2 = ta;
  ta2.position.x = 4.f;
  engine::scene::Transform tb2 = tb;
  tb2.position.x = 5.f;
  world.set_local_transform(a, ta2);
  world.set_local_transform(b, tb2);
  editor::UndoStack undo;
  undo.PushBatch({a, b}, {ta, tb}, {ta2, tb2});
  REQUIRE(undo.Undo(world));
  REQUIRE(world.local_transform(a).position.x == 0.f);
  REQUIRE(world.local_transform(b).position.x == 1.f);
  REQUIRE(undo.Redo(world));
  REQUIRE(world.local_transform(a).position.x == 4.f);
}

TEST_CASE("duplicate node copies transform and mesh", "[ed6]") {
  engine::scene::World world;
  const auto src = world.CreateNode("prop");
  engine::scene::Transform t;
  t.position = {2.f, 0.5f, 0.f};
  world.set_local_transform(src, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(src, mesh);
  const auto copy = editor::DuplicateNode(world, src, 1.f);
  REQUIRE(copy != engine::scene::kInvalidNode);
  REQUIRE(copy != src);
  REQUIRE(world.local_transform(copy).position.x == 3.f);
  REQUIRE(world.mesh(copy) != nullptr);
  REQUIRE(world.mesh(copy)->mesh_id == "cube");
}

TEST_CASE("destroy selection removes nodes", "[ed6]") {
  engine::scene::World world;
  const auto a = world.CreateNode("a");
  editor::Selection sel;
  sel.Set(a);
  editor::DestroySelection(world, &sel);
  REQUIRE(!world.valid(a));
  REQUIRE(sel.node == engine::scene::kInvalidNode);
}

TEST_CASE("frame camera looks at node", "[ed6]") {
  engine::scene::World world;
  const auto n = world.CreateNode("n");
  engine::scene::Transform t;
  t.position = {10.f, 1.f, 4.f};
  world.set_local_transform(n, t);
  engine::render::Camera cam;
  editor::FrameCamera(&cam, world, n);
  REQUIRE(cam.position.x == 10.f);
  REQUIRE(cam.position.z == 10.f);
}

TEST_CASE("ray hits gizmo x axis", "[gizmo]") {
  editor::Ray ray;
  ray.origin = {0.6f, 0.f, -2.f};
  ray.dir = {0.f, 0.f, 1.f};
  const auto hit = editor::HitGizmoAxes(ray, {0.f, 0.f, 0.f}, 1.4f, 0.2f);
  REQUIRE(hit == editor::Axis::X);
}

TEST_CASE("gizmo rotate and scale change transform", "[gizmo]") {
  engine::scene::World world;
  const auto n = world.CreateNode("cube");
  engine::scene::Transform origin{};
  origin.position = {0.f, 0.f, 0.f};
  origin.scale = {1.f, 1.f, 1.f};
  world.set_local_transform(n, origin);
  std::vector<engine::scene::NodeId> ids{n};
  std::vector<engine::scene::Transform> origins{origin};
  REQUIRE(editor::ApplyGizmo(world, ids, origins, editor::GizmoMode::Rotate, editor::Axis::Y, 0.5f,
                             false, 1.f));
  REQUIRE(world.local_transform(n).rotation.y != 0.f);
  REQUIRE(editor::ApplyGizmo(world, ids, origins, editor::GizmoMode::Scale, editor::Axis::X, 1.f, false,
                             1.f));
  REQUIRE(world.local_transform(n).scale.x > 1.5f);
  REQUIRE(editor::ApplyGizmo(world, ids, origins, editor::GizmoMode::Move, editor::Axis::Y, 2.f, false,
                             1.f));
  REQUIRE(world.local_transform(n).position.y == 2.f);
}

TEST_CASE("content browser scans json", "[content]") {
  const auto dir = std::filesystem::temp_directory_path() / "editor_content_scan";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  {
    std::ofstream out(dir / "prop.json");
    out << "{\"format_version\":1,\"nodes\":[{\"id\":\"1\",\"name\":\"p\",\"parent\":null,"
           "\"transform\":{\"t\":[0,0,0],\"r\":[0,0,0,1],\"s\":[1,1,1]},\"visible\":true,"
           "\"prefab_id\":\"prop\",\"components\":[]}]}";
  }
  editor::ContentBrowser browser;
  browser.Scan({dir});
  REQUIRE(!browser.items.empty());
  REQUIRE(browser.items[0].kind == editor::ContentItem::Kind::Prefab);
}

TEST_CASE("prefab from selection roundtrip", "[content]") {
  engine::scene::World world;
  const auto n = world.CreateNode("crate");
  engine::scene::Transform t;
  t.position = {2.f, 0.5f, 0.f};
  world.set_local_transform(n, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(n, mesh);
  const auto path = std::filesystem::temp_directory_path() / "editor_sel_prefab.json";
  REQUIRE(editor::SaveSelectionPrefab(world, n, path).ok());
  auto loaded = game_kit::LoadPrefabDocument(path);
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().prefab_id == "crate");
  REQUIRE(!loaded.value().scene.nodes.empty());
}

TEST_CASE("scene play moves named player", "[play]") {
  engine::scene::World world;
  const auto p = world.CreateNode("player");
  engine::scene::Transform t;
  t.position = {0.f, 0.5f, 0.f};
  world.set_local_transform(p, t);
  REQUIRE(editor::FindNamed(world, "player") == p);
  editor::MovePlayerOnGround(world, p, 0.f, {0.f, 0.f, 1.f}, 5.5f, 1.f);
  REQUIRE(world.local_transform(p).position.z < 0.f);
  engine::render::Camera cam;
  cam.yaw = 0.f;
  cam.pitch = 0.f;
  editor::FollowPlayerCamera(&cam, world.local_transform(p).position);
  REQUIRE(cam.position.y > 1.f);
}

TEST_CASE("gizmo ring hit prefers rotate axis", "[gizmo]") {
  editor::Ray ray;
  ray.origin = {0.85f, 1.f, 0.f};
  ray.dir = {0.f, -1.f, 0.f};
  const auto hit = editor::HitGizmoRotate(ray, {0.f, 0.f, 0.f}, 1.4f, 0.18f, 0.85f, 0.16f);
  REQUIRE(hit == editor::Axis::Y);
}

TEST_CASE("ground clamps player y", "[play]") {
  engine::scene::World world;
  const auto g = world.CreateNode("ground");
  engine::scene::Transform gt;
  gt.scale = {8.f, 1.f, 8.f};
  world.set_local_transform(g, gt);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "ground";
  world.set_mesh(g, mesh);
  REQUIRE(editor::GroundTopY(world, 0.f, 0.f) == 0.5f);
  const auto p = world.CreateNode("player");
  engine::scene::Transform pt;
  pt.position = {0.f, 0.f, 0.f};
  world.set_local_transform(p, pt);
  editor::ResolveGroundY(world, p);
  REQUIRE(world.local_transform(p).position.y == 0.5f);
}

TEST_CASE("restore meta matches by name not index", "[play]") {
  game_kit::SceneDocument stored;
  game_kit::SceneNode a;
  a.id = "9";
  a.name = "chest";
  a.script_path = "editor/scripts/chest.lua";
  stored.nodes.push_back(a);
  game_kit::SceneNode b;
  b.id = "8";
  b.name = "player";
  stored.nodes.push_back(b);
  game_kit::SceneDocument captured;
  game_kit::SceneNode c;
  c.id = "1";
  c.name = "chest";
  captured.nodes.push_back(c);
  game_kit::SceneNode d;
  d.id = "2";
  d.name = "player";
  captured.nodes.push_back(d);
  std::unordered_map<engine::scene::NodeId, editor::NodeMeta> meta;
  editor::RestoreMeta(stored, captured, &meta);
  REQUIRE(meta[1].script_path == "editor/scripts/chest.lua");
  REQUIRE(meta[2].script_path.empty());
}

TEST_CASE("bind play scripts registers entities", "[play]") {
  engine::scene::World world;
  const auto p = world.CreateNode("player");
  const auto c = world.CreateNode("chest");
  std::unordered_map<engine::scene::NodeId, editor::NodeMeta> meta;
  meta[c] = editor::NodeMeta{"", "editor/scripts/chest.lua"};
  game_kit::GameRuntime rt;
  game_kit::ScriptComponentWorld scripts;
  editor::BindPlayScripts(scripts, world, rt, meta);
  REQUIRE(rt.entities().FindByName("player") != nullptr);
  REQUIRE(rt.entities().FindByName("player")->node == p);
  REQUIRE(rt.entities().FindByNode(c) != nullptr);
}

TEST_CASE("undo spawn then redo restores name", "[undo]") {
  engine::scene::World world;
  editor::UndoStack undo;
  std::unordered_map<engine::scene::NodeId, editor::NodeMeta> meta;
  const auto id = world.CreateNode("cube");
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(id, mesh);
  std::vector<editor::NodeSnap> snaps;
  editor::CaptureSubtree(world, id, meta, &snaps);
  undo.PushSpawn(std::move(snaps));
  REQUIRE(undo.Undo(world, &meta));
  REQUIRE(!world.valid(id));
  REQUIRE(undo.Redo(world, &meta));
  REQUIRE(editor::FindNamed(world, "cube") != engine::scene::kInvalidNode);
  const auto again = editor::FindNamed(world, "cube");
  REQUIRE(world.mesh(again) != nullptr);
  REQUIRE(world.mesh(again)->mesh_id == "cube");
}

TEST_CASE("undo kill restores node", "[undo]") {
  engine::scene::World world;
  editor::UndoStack undo;
  std::unordered_map<engine::scene::NodeId, editor::NodeMeta> meta;
  const auto id = world.CreateNode("keep");
  std::vector<editor::NodeSnap> snaps;
  editor::CaptureSubtree(world, id, meta, &snaps);
  undo.PushKill(std::move(snaps));
  (void)world.DestroyNode(id);
  REQUIRE(!world.valid(id));
  REQUIRE(undo.Undo(world, &meta));
  REQUIRE(editor::FindNamed(world, "keep") != engine::scene::kInvalidNode);
}

TEST_CASE("batch visible props on two nodes", "[inspector]") {
  engine::scene::World world;
  const auto a = world.CreateNode("a");
  const auto b = world.CreateNode("b");
  std::unordered_map<engine::scene::NodeId, editor::NodeMeta> meta;
  auto before_a = editor::CaptureProp(world, a, meta);
  auto before_b = editor::CaptureProp(world, b, meta);
  world.set_visible(a, false);
  world.set_visible(b, false);
  auto after_a = editor::CaptureProp(world, a, meta);
  auto after_b = editor::CaptureProp(world, b, meta);
  editor::UndoStack undo;
  undo.PushProps({before_a, before_b}, {after_a, after_b});
  REQUIRE(!world.visible(a));
  REQUIRE(undo.Undo(world, &meta));
  REQUIRE(world.visible(a));
  REQUIRE(world.visible(b));
}

TEST_CASE("local gizmo move x follows rotation", "[gizmo]") {
  engine::scene::World world;
  const auto n = world.CreateNode("rot");
  engine::scene::Transform t;
  t.rotation = engine::Quat::FromEulerYxz(1.5707963f, 0.f, 0.f);
  world.set_local_transform(n, t);
  const auto dir = editor::GizmoAxisDir(editor::Axis::X, t.rotation, true);
  std::vector<engine::scene::NodeId> ids{n};
  std::vector<engine::scene::Transform> origins{t};
  REQUIRE(editor::ApplyGizmo(world, ids, origins, editor::GizmoMode::Move, editor::Axis::X, 1.f, false,
                             1.f, true));
  const auto p = world.local_transform(n).position;
  REQUIRE(std::fabs(p.x - dir.x) < 0.05f);
  REQUIRE(std::fabs(p.z - dir.z) < 0.05f);
}

TEST_CASE("apply op create select set dump", "[cmd]") {
  editor::EditorHost host;
  auto s = host.Bind();
  editor::EditorOp create;
  create.kind = editor::EditorOp::Kind::Create;
  create.create_kind = "cube";
  REQUIRE(editor::ApplyOp(s, create).ok);
  editor::EditorOp sel;
  sel.kind = editor::EditorOp::Kind::Select;
  sel.name = "cube";
  REQUIRE(editor::ApplyOp(s, sel).ok);
  editor::EditorOp xf;
  xf.kind = editor::EditorOp::Kind::SetTransform;
  xf.has_x = true;
  xf.x = 2.f;
  REQUIRE(editor::ApplyOp(s, xf).ok);
  editor::EditorOp dump;
  dump.kind = editor::EditorOp::Kind::Dump;
  const auto r = editor::ApplyOp(s, dump);
  REQUIRE(r.ok);
  REQUIRE(r.json.find("\"name\":\"cube\"") != std::string::npos);
  REQUIRE(r.json.find("\"x\":2") != std::string::npos);
}

TEST_CASE("mcp tools call reaches apply op", "[mcp]") {
  editor::EditorHost host;
  const auto listed = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  REQUIRE(listed.find("editor_dump") != std::string::npos);
  const auto created = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"editor_create","arguments":{"kind":"player"}}})");
  REQUIRE(created.find("isError\":false") != std::string::npos);
  const auto dumped = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"editor_dump","arguments":{}}})");
  REQUIRE(dumped.find("player") != std::string::npos);
}

TEST_CASE("set parent reparents and rejects cycle", "[hierarchy]") {
  engine::scene::World world;
  const auto a = world.CreateNode("a");
  const auto b = world.CreateNode("b");
  REQUIRE(world.set_parent(b, a));
  REQUIRE(world.parent(b) == a);
  REQUIRE(!world.set_parent(a, b));
  REQUIRE(world.set_parent(b, engine::scene::kInvalidNode));
  REQUIRE(world.parent(b) == engine::scene::kInvalidNode);
}

TEST_CASE("node meta extra encodes light", "[io]") {
  editor::NodeMeta m;
  m.has_light = true;
  m.light_range = 12.f;
  m.material_id = "cube";
  const auto json = editor::EncodeNodeMeta(m);
  editor::NodeMeta out;
  editor::DecodeNodeMetaExtra(json, &out);
  REQUIRE(out.has_light);
  REQUIRE(std::fabs(out.light_range - 12.f) < 0.01f);
  REQUIRE(out.material_id == "cube");
}

TEST_CASE("mcp lists set_parent bake lint", "[mcp]") {
  editor::EditorHost host;
  const auto listed = editor::HandleMcpLine(
      host, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  REQUIRE(listed.find("editor_set_parent") != std::string::npos);
  REQUIRE(listed.find("editor_bake") != std::string::npos);
  REQUIRE(listed.find("editor_lint") != std::string::npos);
}
