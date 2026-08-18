#include "game_kit/anim_player.h"
#include "game_kit/audio_mixer.h"
#include "game_kit/entity.h"
#include "game_kit/level_flow.h"
#include "game_kit/nav.h"
#include "game_kit/player_controller.h"
#include "game_kit/runtime.h"
#include "game_kit/script_component.h"
#include "game_kit/save.h"
#include "game_kit/snapshot.h"
#include "game_kit/timeline.h"
#include "kit_test.h"

#include "engine/core/math.h"
#include "engine/input/input_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("entity tags and object pool", "[gk-entity]") {
  game_kit::EntityWorld w;
  const auto id = w.Create("bullet");
  auto* e = w.Get(id);
  REQUIRE(e != nullptr);
  e->AddTag("projectile");
  e->AddTag("enemy");
  REQUIRE(e->HasTag("projectile"));
  REQUIRE(w.FindByTag("projectile").size() == 1);

  w.Release(id);
  REQUIRE(w.FindByName("bullet") == nullptr);
  const auto id2 = w.Acquire("bullet");
  REQUIRE(id2 == id);
  REQUIRE(w.FindByName("bullet") != nullptr);
}

TEST_CASE("level additive and async delay", "[gk-level]") {
  game_kit::GameRuntime rt;
  game_kit::LevelFlow flow;
  flow.Register("hub", [](engine::Application&, game_kit::GameRuntime&) {});
  flow.Register("dungeon", [](engine::Application&, game_kit::GameRuntime&) {});

  REQUIRE(flow.Request("hub", game_kit::LoadMode::Replace, 0.4f).ok());
  flow.Pump(rt, 0.2f);
  REQUIRE(flow.loading());
  REQUIRE(flow.loading_progress() > 0.4f);
  REQUIRE(flow.current().empty());
  flow.Pump(rt, 0.3f);
  REQUIRE(flow.current() == "hub");
  REQUIRE(flow.stacked().size() == 1);

  REQUIRE(flow.Request("dungeon", game_kit::LoadMode::Additive).ok());
  flow.Pump(rt, 0.f);
  REQUIRE(flow.current() == "dungeon");
  REQUIRE(flow.stacked().size() == 2);

  REQUIRE(flow.Request("hub", game_kit::LoadMode::Replace).ok());
  flow.Pump(rt, 0.f);
  REQUIRE(flow.current() == "hub");
  REQUIRE(flow.stacked().size() == 1);
}

TEST_CASE("player action move jump and cameras", "[gk-player]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("player");
  rt.entities().Create("player", n);

  engine::input::InputSystem input;
  game_kit::PlayerController::InstallPlayDefaults(input);
  game_kit::PlayerController pc;
  pc.entity_name = "player";
  pc.use_actions = true;
  pc.ground_y = 0.f;

  input.set_key(engine::input::Key::W, true);
  pc.TickMove(input, rt, 0.1f);
  REQUIRE(world.local_transform(n).position.z > 0.f);

  input.set_key(engine::input::Key::W, false);
  input.set_key(engine::input::Key::Space, true);
  pc.TickMove(input, rt, 0.016f);
  REQUIRE(!pc.grounded);
  REQUIRE(world.local_transform(n).position.y > 0.f);

  engine::render::Camera cam;
  pc.camera_pitch = 0.f;
  pc.camera_yaw = 0.f;
  pc.camera_mode = game_kit::CameraMode::FirstPerson;
  pc.TickView(cam, rt);
  REQUIRE(std::fabs(cam.position.y - (world.local_transform(n).position.y + pc.arm_height)) < 0.01f);

  pc.camera_mode = game_kit::CameraMode::SpringArm;
  pc.TickView(cam, rt);
  REQUIRE(cam.position.z > world.local_transform(n).position.z);
}

TEST_CASE("physics aabb contact dispatches collision", "[gk-phys]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  auto phys = engine::physics::CreateBuiltinPhysicsWorld();
  REQUIRE(phys != nullptr);
  rt.set_physics(phys.get());

  const auto na = world.CreateNode("trig");
  const auto nb = world.CreateNode("body");
  const auto ida = rt.entities().Create("trig", na);
  const auto idb = rt.entities().Create("body", nb);
  auto* ea = rt.entities().Get(ida);
  auto* eb = rt.entities().Get(idb);
  REQUIRE(ea != nullptr);
  REQUIRE(eb != nullptr);

  engine::physics::RigidBodyDesc da;
  da.position = {0.f, 0.f, 0.f};
  da.half_extents = {1.f, 1.f, 1.f};
  da.mass = 0.f;
  ea->physics_body = phys->CreateBox(da);
  ea->physics_is_trigger = true;
  engine::physics::RigidBodyDesc db;
  db.position = {0.5f, 0.f, 0.f};
  db.half_extents = {0.5f, 0.5f, 0.5f};
  db.mass = 0.f;
  eb->physics_body = phys->CreateBox(db);

  std::string got;
  rt.events().Subscribe("collision.enter", [&](std::string_view p) { got = std::string(p); });
  rt.TickLogic(0.016f);
  REQUIRE(got == "body");
}

TEST_CASE("anim player notify and mixer attenuation", "[gk-anim][gk-audio]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("hero");
  rt.entities().Create("hero", n);

  auto& ap = rt.anims().GetOrCreate("hero");
  ap.Play("idle", true);
  ap.AddNotify("idle", "foot", 0.25f);

  std::string notify;
  rt.events().Subscribe("anim.notify", [&](std::string_view p) { notify = std::string(p); });
  rt.TickLogic(0.3f);
  REQUIRE(notify == "foot");
  REQUIRE(ap.root_motion_delta().length_squared() > 0.f);

  rt.mixer().set_listener({0.f, 0.f, 0.f});
  rt.mixer().Play("near", "x.wav", 1.f, true, {0.f, 0.f, 0.f});
  rt.mixer().Play("far", "x.wav", 1.f, true, {10.f, 0.f, 0.f});
  rt.mixer().Tick();
  auto* near_v = rt.mixer().Find("near");
  auto* far_v = rt.mixer().Find("far");
  REQUIRE(near_v != nullptr);
  REQUIRE(far_v != nullptr);
  REQUIRE(near_v->last_output_gain > far_v->last_output_gain);
}

TEST_CASE("nav steer slides aabb and sense chase", "[gk-nav]") {
  engine::scene::World world;
  game_kit::EntityWorld entities;
  const auto h = world.CreateNode("hunter");
  const auto p = world.CreateNode("prey");
  engine::scene::Transform t;
  t.position = {0.f, 0.f, 0.f};
  world.set_local_transform(h, t);
  t.position = {1.f, 0.f, 0.f};
  world.set_local_transform(p, t);
  entities.Create("hunter", h);
  entities.Create("prey", p);

  game_kit::NavWorld nav;
  nav.AddObstacle({0.f, 0.f, 0.5f}, {0.4f, 1.f, 0.4f});
  const auto next = nav.Steer({0.f, 0.f, 0.f}, {0.f, 0.f, 2.f}, 1.f, 0.5f);
  REQUIRE(next.z < 0.4f);

  nav.TickSense(entities, &world, "hunter", "prey", 2.f);
  REQUIRE(entities.FindByName("hunter")->ai.state == game_kit::AiState::Chase);
  t.position = {20.f, 0.f, 0.f};
  world.set_local_transform(p, t);
  nav.TickSense(entities, &world, "hunter", "prey", 2.f);
  REQUIRE(entities.FindByName("hunter")->ai.state == game_kit::AiState::Idle);
}

TEST_CASE("timeline cues and snapshot roundtrip", "[gk-snap]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("player");
  engine::scene::Transform t;
  t.position = {3.f, 1.f, 2.f};
  world.set_local_transform(n, t);
  rt.entities().Create("player", n);
  rt.entities().FindByName("player")->ai.Set(game_kit::AiState::Patrol);

  std::string cue;
  rt.events().Subscribe("cut", [&](std::string_view p) { cue = std::string(p); });
  rt.timeline().Add(0.2f, "cut", "go");
  rt.timeline().Play();
  rt.TickLogic(0.1f);
  REQUIRE(cue.empty());
  rt.TickLogic(0.15f);
  REQUIRE(cue == "go");

  const auto dir = std::filesystem::temp_directory_path() / "game_kit_snap";
  game_kit::SaveSlots slots(dir);
  auto snap = game_kit::CaptureSnapshot(rt, &world);
  REQUIRE(game_kit::SaveSnapshot(slots, 0, snap).ok());
  auto loaded = game_kit::LoadSnapshot(slots, 0);
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().entities.size() == 1);
  REQUIRE(loaded.value().entities[0].name == "player");

  t.position = {};
  world.set_local_transform(n, t);
  REQUIRE(game_kit::ApplySnapshot(rt, &world, loaded.value()).ok());
  REQUIRE(std::fabs(world.local_transform(n).position.x - 3.f) < 0.01f);

  rt.replicator().Push(snap);
  REQUIRE(rt.replicator().has());
  (void)rt.replicator().Pull();
  REQUIRE(!rt.replicator().has());
}

TEST_CASE("root motion applies to entity", "[gk-anim]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("hero");
  rt.entities().Create("hero", n);
  rt.anims().GetOrCreate("hero").Play("idle", true);
  rt.TickLogic(0.3f);
  REQUIRE(world.local_transform(n).position.z > 0.1f);
}

TEST_CASE("player look and camera-relative sprint", "[gk-player]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("player");
  rt.entities().Create("player", n);
  engine::input::InputSystem input;
  game_kit::PlayerController pc;
  pc.entity_name = "player";
  pc.look_sensitivity = 0.1f;
  input.set_mouse_delta(5.f, 0.f);
  pc.TickMove(input, rt, 0.016f);
  REQUIRE(pc.camera_yaw > 0.f);

  input.set_mouse_delta(0.f, 0.f);
  input.set_key(engine::input::Key::W, true);
  input.set_key(engine::input::Key::E, true);
  const float z0 = world.local_transform(n).position.z;
  pc.TickMove(input, rt, 0.1f);
  REQUIRE(world.local_transform(n).position.z > z0);
}

TEST_CASE("timeline seek and pause", "[gk-timeline]") {
  game_kit::GameRuntime rt;
  int n = 0;
  rt.events().Subscribe("cue", [&](std::string_view) { ++n; });
  rt.timeline().Add(0.5f, "cue", "a");
  rt.timeline().Play();
  rt.TickLogic(0.6f);
  REQUIRE(n == 1);
  rt.timeline().Seek(0.f);
  rt.timeline().Pause();
  rt.TickLogic(1.f);
  REQUIRE(n == 1);
  rt.timeline().Play();
  rt.TickLogic(0.6f);
  REQUIRE(n == 2);
}

TEST_CASE("nav follow waypoints", "[gk-nav]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("npc");
  rt.entities().Create("npc", n);
  rt.nav().SetPath("npc", {{0.f, 0.f, 2.f}, {2.f, 0.f, 2.f}}, 8.f);
  for (int i = 0; i < 40; ++i) {
    rt.TickLogic(0.05f);
  }
  REQUIRE(rt.nav().PathFinished("npc"));
  REQUIRE(world.local_transform(n).position.x > 1.f);
}

TEST_CASE("mixer named buses", "[gk-audio]") {
  game_kit::AudioMixer mix;
  mix.Play("a", "x.wav", 1.f, false, {}, "sfx");
  mix.Play("b", "x.wav", 1.f, false, {}, "music");
  mix.set_bus_gain("sfx", 0.5f);
  mix.set_bus_gain("music", 1.f);
  mix.Tick();
  REQUIRE(mix.Find("a")->last_output_gain < mix.Find("b")->last_output_gain);
}

TEST_CASE("loopback push diff", "[gk-snap]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("player");
  rt.entities().Create("player", n);
  rt.replicator().PushDiff(game_kit::CaptureSnapshot(rt, &world));
  REQUIRE(rt.replicator().has_diff());
  (void)rt.replicator().Pull();
  rt.replicator().PushDiff(game_kit::CaptureSnapshot(rt, &world));
  REQUIRE(!rt.replicator().has_diff());
  engine::scene::Transform t;
  t.position = {4.f, 0.f, 0.f};
  world.set_local_transform(n, t);
  rt.replicator().PushDiff(game_kit::CaptureSnapshot(rt, &world));
  REQUIRE(rt.replicator().has_diff());
  REQUIRE(rt.replicator().Pull().entities.size() == 1);
}

TEST_CASE("unload stacked additive level", "[gk-level]") {
  game_kit::GameRuntime rt;
  game_kit::LevelFlow flow;
  flow.Register("hub", [](engine::Application&, game_kit::GameRuntime&) {});
  flow.Register("dungeon", [](engine::Application&, game_kit::GameRuntime&) {});
  REQUIRE(flow.Request("hub").ok());
  flow.Pump(rt, 0.f);
  REQUIRE(flow.Request("dungeon", game_kit::LoadMode::Additive).ok());
  flow.Pump(rt, 0.f);
  REQUIRE(flow.stacked().size() == 2);
  REQUIRE(flow.UnloadStacked("dungeon", rt).ok());
  REQUIRE(flow.stacked().size() == 1);
  REQUIRE(flow.current() == "hub");
}

TEST_CASE("snapshot version rot tags persist", "[gk-snap]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("hero");
  auto* e = rt.entities().Get(rt.entities().Create("hero", n));
  REQUIRE(e);
  e->AddTag("player");
  e->script_path = "hero.lua";
  engine::scene::Transform t;
  t.position = {2.f, 3.f, 4.f};
  t.scale = {2.f, 2.f, 2.f};
  world.set_local_transform(n, t);
  const auto snap = game_kit::CaptureSnapshot(rt, &world);
  REQUIRE(snap.format_version == game_kit::kSnapshotFormatCurrent);
  REQUIRE(snap.entities.size() == 1);
  REQUIRE(snap.entities[0].position.x == 2.f);
  REQUIRE(snap.entities[0].scale.x == 2.f);
  REQUIRE(snap.entities[0].script_path == "hero.lua");
  REQUIRE(snap.entities[0].tags.size() == 1);

  const auto dir = std::filesystem::temp_directory_path() / "game_kit_snap_v1";
  game_kit::SaveSlots slots(dir);
  REQUIRE(game_kit::SaveSnapshot(slots, 1, snap).ok());
  auto loaded = game_kit::LoadSnapshot(slots, 1);
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().entities[0].position.z == 4.f);
  REQUIRE(loaded.value().entities[0].script_path == "hero.lua");

  const auto v0 = dir / "slot_2.json";
  {
    std::ofstream out(v0);
    out << "{\"slot\":2,\"payload\":\"{\\\"level\\\":\\\"x\\\",\\\"entities\\\":[]}\"}";
  }
  auto old = slots.Read(2);
  REQUIRE(old.ok());
  REQUIRE(old.value().version == 0);
}

TEST_CASE("replication session interpolates replica", "[gk-snap]") {
  engine::scene::World server_w;
  engine::scene::World client_w;
  game_kit::GameRuntime server;
  game_kit::GameRuntime client;
  server.set_world(&server_w);
  client.set_world(&client_w);
  const auto ns = server_w.CreateNode("p");
  const auto nc = client_w.CreateNode("p");
  server.entities().Create("p", ns);
  client.entities().Create("p", nc);
  engine::scene::Transform t;
  t.position = {10.f, 0.f, 0.f};
  server_w.set_local_transform(ns, t);
  game_kit::ReplicationSession session;
  session.ServerCapture(server, &server_w);
  REQUIRE(session.has_frame());
  session.ClientApply(client, &client_w, 1.f);
  REQUIRE(client_w.local_transform(nc).position.x > 5.f);
}

TEST_CASE("player grounded from physics raycast", "[gk-player]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("player");
  rt.entities().Create("player", n);
  auto phys = engine::physics::CreateBuiltinPhysicsWorld();
  engine::physics::RigidBodyDesc floor;
  floor.position = {0.f, -0.5f, 0.f};
  floor.half_extents = {4.f, 0.5f, 4.f};
  floor.mass = 0.f;
  (void)phys->CreateBox(floor);
  engine::physics::RigidBodyDesc body;
  body.position = {0.f, 1.f, 0.f};
  body.half_extents = {0.4f, 0.9f, 0.4f};
  body.mass = 0.f;
  game_kit::PlayerController pc;
  pc.entity_name = "player";
  pc.use_actions = false;
  pc.physics_body = phys->CreateBox(body);
  pc.grounded = false;
  rt.set_physics(phys.get());
  engine::input::InputSystem input;
  pc.TickMove(input, rt, 0.016f);
  REQUIRE(pc.grounded);
  REQUIRE(world.local_transform(n).position.y > 0.f);
}

TEST_CASE("snapshot diff includes rotation", "[gk-snap]") {
  game_kit::LoopbackReplicator r;
  game_kit::WorldSnapshot a;
  game_kit::EntitySnap e;
  e.name = "p";
  a.entities.push_back(e);
  r.Push(a);
  game_kit::WorldSnapshot b = a;
  b.entities[0].rotation = engine::Quat::FromEulerYxz(1.2f, 0.f, 0.f);
  r.PushDiff(b);
  REQUIRE(r.has_diff());
}

TEST_CASE("replication session slerps rotation", "[gk-snap]") {
  engine::scene::World server_w;
  engine::scene::World client_w;
  game_kit::GameRuntime server;
  game_kit::GameRuntime client;
  server.set_world(&server_w);
  client.set_world(&client_w);
  const auto ns = server_w.CreateNode("p");
  const auto nc = client_w.CreateNode("p");
  server.entities().Create("p", ns);
  client.entities().Create("p", nc);
  engine::scene::Transform t;
  t.rotation = engine::Quat::FromEulerYxz(1.5f, 0.f, 0.f);
  server_w.set_local_transform(ns, t);
  game_kit::ReplicationSession session;
  session.ServerCapture(server, &server_w);
  session.ClientApply(client, &client_w, 0.01f);
  const auto got = client_w.local_transform(nc).rotation;
  REQUIRE(std::abs(engine::Dot(got, t.rotation)) < 0.999f);
  REQUIRE(std::abs(engine::Dot(got, engine::Quat::Identity())) < 0.999f);
}

TEST_CASE("nav bake findpath fallback", "[gk-nav]") {
  game_kit::NavWorld nav;
  nav.AddObstacle({0.f, 0.5f, 0.f}, {1.f, 0.5f, 1.f});
  const bool baked = nav.BakeFromObstacles();
  const auto path = nav.FindPath({-4.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  REQUIRE(!path.empty());
  if (baked) {
    REQUIRE(nav.has_navmesh());
    REQUIRE(path.size() >= 2);
    for (const auto& p : path) {
      REQUIRE(!(std::abs(p.x) < 0.5f && std::abs(p.z) < 0.5f && p.y < 0.35f));
    }
  }
}

TEST_CASE("nav bake from physics and crowd", "[gk-nav]") {
  auto phys = engine::physics::CreateBuiltinPhysicsWorld();
  engine::physics::RigidBodyDesc floor;
  floor.position = {0.f, -0.25f, 0.f};
  floor.half_extents = {12.f, 0.25f, 12.f};
  floor.mass = 0.f;
  (void)phys->CreateBox(floor);
  engine::physics::RigidBodyDesc wall;
  wall.position = {0.f, 0.5f, 0.f};
  wall.half_extents = {1.f, 0.5f, 1.f};
  wall.mass = 0.f;
  (void)phys->CreateBox(wall);
  game_kit::NavWorld nav;
  const bool baked = nav.BakeFromPhysics(*phys);
  if (!baked) {
    return;
  }
  REQUIRE(nav.has_navmesh());
  const int a = nav.AddAgent("a", {-3.f, 0.f, -2.f});
  const int b = nav.AddAgent("b", {3.f, 0.f, 2.f});
  if (a < 0 || b < 0) {
    return;
  }
  nav.SetAgentTarget("a", {3.f, 0.f, 2.f});
  nav.SetAgentTarget("b", {-3.f, 0.f, -2.f});
  for (int i = 0; i < 30; ++i) {
    nav.TickCrowd(0.05f);
  }
  const auto pa = nav.AgentPosition("a");
  const auto pb = nav.AgentPosition("b");
  REQUIRE((pa - pb).length_squared() > 0.01f);
}

TEST_CASE("nav follow stays on mesh", "[gk-nav]") {
  engine::scene::World world;
  game_kit::EntityWorld entities;
  const auto n = world.CreateNode("npc");
  engine::scene::Transform t;
  t.position = {-4.f, 0.f, 0.f};
  world.set_local_transform(n, t);
  entities.Create("npc", n);
  game_kit::NavWorld nav;
  nav.AddObstacle({0.f, 0.5f, 0.f}, {1.f, 0.5f, 1.f});
  if (!nav.BakeFromObstacles()) {
    nav.SetPath("npc", {{-4.f, 0.f, 0.f}, {4.f, 0.f, 0.f}});
    nav.TickFollow(entities, &world, 0.1f);
    REQUIRE(world.local_transform(n).position.x > -4.f);
    return;
  }
  auto pts = nav.FindPath({-4.f, 0.f, 0.f}, {4.f, 0.f, 0.f});
  nav.SetPath("npc", pts);
  for (int i = 0; i < 40; ++i) {
    nav.TickFollow(entities, &world, 0.05f);
  }
  const auto p = world.local_transform(n).position;
  REQUIRE(p.x > -4.f);
}
