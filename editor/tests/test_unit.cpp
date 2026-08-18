#include "editing/anim_edit.h"
#include "editing/terrain_edit.h"
#include "editing/tile_edit.h"
#include "editing/viewport_layout.h"
#include "io/dep_graph.h"

#include "game_kit/script_fields.h"

#include "kit_test.h"

#include "engine/scene/world.h"

#include <cmath>
#include <fstream>
#include <filesystem>

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
