#include "editing/anim_edit.h"
#include "editing/settings.h"
#include "editing/sprite_view.h"
#include "editing/terrain_edit.h"
#include "editing/tile_edit.h"
#include "editing/viewport_layout.h"
#include "io/content_browser.h"
#include "io/dep_graph.h"
#include "io/scene_ext.h"

#include "game_kit/scene_document.h"
#include "game_kit/script_fields.h"

#include "kit_test.h"

#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include <cmath>
#include <fstream>
#include <filesystem>

TEST_CASE("W25 editor settings output dock defaults", "[unit][w25]") {
  editor::EditorSettings s;
  REQUIRE(s.show_output);
  REQUIRE(!s.status_line.empty());
  s.output_lines.push_back("saved demo");
  REQUIRE(s.output_lines.size() == 1);
  s.show_output = false;
  REQUIRE(!s.show_output);
}

TEST_CASE("world light camera collider sprite components", "[unit]") {
  engine::scene::World world;
  const auto id = world.CreateNode("lamp");
  engine::scene::LightComponent L;
  L.range = 9.f;
  world.set_light(id, L);
  REQUIRE(world.light(id) != nullptr);
  REQUIRE(std::fabs(world.light(id)->range - 9.f) < 0.01f);
  engine::scene::CameraComponent cam;
  cam.active = true;
  world.set_camera(id, cam);
  REQUIRE(world.camera(id) != nullptr);
  REQUIRE(world.camera(id)->active);
  engine::scene::ColliderComponent col;
  col.hx = 1.5f;
  world.set_collider(id, col);
  REQUIRE(world.collider(id) != nullptr);
  engine::scene::SpriteComponent spr;
  spr.gid = 3;
  world.set_sprite(id, spr);
  REQUIRE(world.sprite(id) != nullptr);
  REQUIRE(world.sprite(id)->gid == 3);
  world.clear_light(id);
  REQUIRE(world.light(id) == nullptr);
}

TEST_CASE("script export fields parse", "[unit]") {
  const char* src = "--@export speed:number=5\n--@export name:string=hero\nlocal x = 1\n";
  const auto fields = game_kit::ParseScriptExports(src);
  REQUIRE(fields.size() == 2);
  REQUIRE(fields[0].name == "speed");
  REQUIRE(fields[0].value == "5");
  REQUIRE(fields[1].name == "name");
  const auto blob = game_kit::FieldsToPersist(fields);
  REQUIRE(blob.find("speed=5") != std::string::npos);
  const auto merged = game_kit::MergeExportsAndPersist(src, R"({"speed":9,"name":"npc"})");
  REQUIRE(merged.find("speed=9") != std::string::npos);
  REQUIRE(merged.find("name=npc") != std::string::npos);
}

TEST_CASE("viewport quad layout panes", "[unit]") {
  editor::ViewportPane panes[4];
  int n = 0;
  editor::LayoutViewports(1, 800.f, 600.f, panes, &n);
  REQUIRE(n == 4);
  REQUIRE(editor::PaneAt(panes, n, 10.f, 10.f) == 0);
  REQUIRE(editor::PaneAt(panes, n, 500.f, 10.f) == 1);
  REQUIRE(editor::PaneAt(panes, n, 10.f, 400.f) == 2);
  REQUIRE(std::fabs(editor::PaneAspect(panes[0]) - (400.f / 300.f)) < 0.01f);
}

TEST_CASE("terrain brush raise heights", "[unit]") {
  std::vector<float> h;
  editor::RaiseHeight(&h, 8, 8, 1.f, 2.f);
  REQUIRE(h.size() == static_cast<std::size_t>(17 * 17));
  REQUIRE(h[static_cast<std::size_t>(8 * 17 + 8)] > 0.9f);
  const auto map = editor::HeightsToMap(h, 1.f);
  REQUIRE(map.width == 17);
  REQUIRE(map.samples.size() == h.size());
}

TEST_CASE("tile paint writes gid grid", "[unit]") {
  std::vector<int> tiles;
  editor::PaintTile(&tiles, nullptr, 3, 4, 7);
  REQUIRE(tiles[static_cast<std::size_t>(4 * 16 + 3)] == 7);
}

TEST_CASE("anim graph curve and machine", "[unit]") {
  editor::AnimGraphEdit g;
  editor::AddState(&g, "jump");
  REQUIRE(editor::CurrentState(g) == "idle");
  g.current = 1;
  REQUIRE(editor::CurrentState(g) == "walk");
  const float mid = editor::SampleCurve(g, 0.5f);
  REQUIRE(mid > 0.f);
  auto sm = editor::BuildMachine(g);
  REQUIRE(sm.current_state() == "walk");
}

TEST_CASE("dep graph from missing manifest", "[unit]") {
  const auto g = editor::BuildDepGraph(std::filesystem::path("no_such_manifest.json"));
  REQUIRE(!g.ok);
  const auto json = editor::DepGraphJson(g);
  REQUIRE(json.find("\"ok\":false") != std::string::npos);
}

TEST_CASE("viewport split cameras differ yaw and fovy", "[unit]") {
  engine::render::Camera persp;
  persp.yaw = 0.4f;
  persp.fovy_rad = 1.047f;
  engine::render::Camera top;
  engine::render::Camera front;
  engine::render::Camera side;
  editor::ApplyPaneCamera(0, &top, persp);
  editor::ApplyPaneCamera(1, &top, persp);
  editor::ApplyPaneCamera(2, &front, persp);
  editor::ApplyPaneCamera(3, &side, persp);
  REQUIRE(top.ortho);
  REQUIRE(front.ortho);
  REQUIRE(side.ortho);
  REQUIRE(std::fabs(top.pitch + 1.5707963f) < 0.02f);
  REQUIRE(std::fabs(front.yaw - 0.f) < 0.01f);
  REQUIRE(std::fabs(side.yaw + 1.57f) < 0.02f);
  REQUIRE(top.ortho_height > 1.f);
  REQUIRE(std::fabs(front.fovy_rad - side.fovy_rad) < 0.01f);
}

TEST_CASE("terrain lower and smooth brushes", "[unit]") {
  std::vector<float> h;
  editor::RaiseHeight(&h, 8, 8, 2.f, 2.f);
  const float peak = h[static_cast<std::size_t>(8 * 17 + 8)];
  editor::LowerHeight(&h, 8, 8, 1.f, 2.f);
  REQUIRE(h[static_cast<std::size_t>(8 * 17 + 8)] < peak);
  editor::SmoothHeight(&h, 8, 8, 2.f);
  REQUIRE(h.size() == static_cast<std::size_t>(17 * 17));
}

TEST_CASE("validate scene document parent cycle", "[unit]") {
  game_kit::SceneDocument doc;
  doc.format_version = 3;
  game_kit::SceneNode a;
  a.id = "a";
  a.name = "a";
  a.parent = "b";
  game_kit::SceneNode b;
  b.id = "b";
  b.name = "b";
  b.parent = "a";
  doc.nodes.push_back(a);
  doc.nodes.push_back(b);
  REQUIRE(!game_kit::ValidateSceneDocument(doc).ok());
}

TEST_CASE("camera ortho projection differs from perspective", "[unit]") {
  engine::render::Camera cam;
  cam.position = {0.f, 20.f, 0.01f};
  cam.yaw = 0.f;
  cam.pitch = -1.55f;
  const auto persp = cam.proj_matrix(16.f / 9.f);
  cam.ortho = true;
  cam.ortho_height = 16.f;
  const auto ortho = cam.proj_matrix(16.f / 9.f);
  REQUIRE(std::fabs(persp.m[0] - ortho.m[0]) > 0.01f);
  REQUIRE(std::fabs(ortho.m[11]) < 1e-4f);
}

TEST_CASE("world point projects inside ortho viewport", "[unit]") {
  engine::render::Camera cam;
  editor::ApplyOrtho2DCamera(&cam);
  cam.position.x = 8.f;
  cam.position.z = 8.f;
  engine::Vec2 s{};
  REQUIRE(editor::ProjectWorldToScreen(cam.view_proj_matrix(1.f), {8.f, 0.f, 8.f}, 800.f, 800.f, &s));
  REQUIRE(s.x > 50.f);
  REQUIRE(s.x < 750.f);
  REQUIRE(s.y > 50.f);
  REQUIRE(s.y < 750.f);
}

TEST_CASE("content json thumbs are stable not random", "[unit]") {
  const auto dir = std::filesystem::temp_directory_path() / "editor_thumb_scan";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  {
    std::ofstream out(dir / "a.json");
    out << R"({"format_version":3,"nodes":[]})";
  }
  editor::ContentBrowser b1;
  b1.Scan({dir});
  editor::ContentBrowser b2;
  b2.Scan({dir});
  REQUIRE(!b1.items.empty());
  REQUIRE(std::fabs(b1.items.front().thumb_r - b2.items.front().thumb_r) < 1e-6f);
  REQUIRE(b1.items.front().thumb_g > 0.2f);
}

TEST_CASE("scene_ext pack unpack restores anim transitions", "[unit]") {
  editor::EditorSettings src;
  src.anim.states = {"idle", "walk"};
  src.anim.transitions = {{"idle", "walk"}, {"walk", "idle"}};
  src.anim.keys[0] = 0.1f;
  src.anim.keys[3] = 0.9f;
  src.anim.current = 1;
  game_kit::SceneDocument doc;
  editor::PackEditorExtensions(src, &doc);
  editor::EditorSettings dst;
  editor::UnpackEditorExtensions(doc, &dst);
  REQUIRE(dst.anim.current == 1);
  REQUIRE(dst.anim.states.size() == 2);
  REQUIRE(dst.anim.transitions.size() == 2);
  REQUIRE(dst.anim.transitions[0].first == "idle");
  REQUIRE(dst.anim.transitions[0].second == "walk");
  REQUIRE(dst.anim.transitions[1].first == "walk");
  REQUIRE(dst.anim.transitions[1].second == "idle");
  REQUIRE(std::fabs(dst.anim.keys[0] - 0.1f) < 1e-4f);
}
