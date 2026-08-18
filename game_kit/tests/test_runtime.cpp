#include "game_kit/entity.h"
#include "game_kit/event_bus.h"
#include "game_kit/level_flow.h"
#include "game_kit/prefab.h"
#include "game_kit/runtime.h"
#include "game_kit/save.h"
#include "game_kit/scene_document.h"
#include "game_kit/script.h"
#include "game_kit/script_component.h"
#include "game_kit/script_hot_reload.h"
#include "game_kit/script_host.h"
#include "game_kit/coroutine_scheduler.h"
#include "game_kit/ai_state.h"
#include "game_kit/timer.h"
#include "game_kit/trigger.h"
#include "kit_test.h"

#include "engine/scene/world.h"
#include "engine/script/i_script_host.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_map>

TEST_CASE("timer delay fires once", "[gk1]") {
  game_kit::Timer t;
  int n = 0;
  t.Delay(0.5f, [&] { ++n; });
  t.Tick(0.4f);
  REQUIRE(n == 0);
  t.Tick(0.2f);
  REQUIRE(n == 1);
  t.Tick(1.f);
  REQUIRE(n == 1);
}

TEST_CASE("event bus pubsub", "[gk1]") {
  game_kit::EventBus bus;
  std::string got;
  bus.Subscribe("hit", [&](std::string_view p) { got = std::string(p); });
  bus.Publish("hit", "ok");
  REQUIRE(got == "ok");
}

TEST_CASE("entity create find", "[gk1]") {
  game_kit::EntityWorld w;
  const auto id = w.Create("player");
  REQUIRE(w.Get(id) != nullptr);
  REQUIRE(w.FindByName("player") != nullptr);
}

TEST_CASE("save slot roundtrip", "[gk1]") {
  const auto dir = std::filesystem::temp_directory_path() / "game_kit_test_saves";
  game_kit::SaveSlots slots(dir);
  REQUIRE(slots.Write(0, "hello").ok());
  auto r = slots.Read(0);
  REQUIRE(r.ok());
  REQUIRE(r.value().payload == "hello");
}

TEST_CASE("scene document world roundtrip", "[ed2]") {
  engine::scene::World world;
  const auto cube = world.CreateNode("cube");
  engine::scene::Transform t;
  t.position = {1.f, 2.f, 3.f};
  world.set_local_transform(cube, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(cube, mesh);

  const auto doc = game_kit::CaptureWorld(world);
  const auto path = std::filesystem::temp_directory_path() / "game_kit_scene.json";
  REQUIRE(game_kit::SaveSceneDocument(doc, path).ok());
  auto loaded = game_kit::LoadSceneDocument(path);
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().format_version == game_kit::kSceneFormatCurrent);
  REQUIRE(!loaded.value().nodes.empty());

  engine::scene::World world2;
  REQUIRE(game_kit::ApplyWorld(world2, loaded.value()).ok());
  REQUIRE(!world2.roots().empty());
}

TEST_CASE("scene document script component roundtrip", "[ed2][prefab]") {
  engine::scene::World world;
  const auto n = world.CreateNode("chest");
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  auto* e = rt.entities().Get(rt.entities().Create("chest", n));
  REQUIRE(e);
  e->script_path = "chest.lua";
  e->AddTag("loot");

  const auto doc = game_kit::CaptureWorld(world, rt);
  REQUIRE(doc.format_version == game_kit::kSceneFormatCurrent);
  REQUIRE(!doc.host_api_hint.empty());
  bool saw_script = false;
  bool saw_tag = false;
  for (const auto& node : doc.nodes) {
    for (const auto& c : node.components) {
      if (c.type == "Script" && c.script == "chest.lua") {
        saw_script = true;
      }
      if (c.type == "GameTag" && c.script == "loot") {
        saw_tag = true;
      }
    }
  }
  REQUIRE(saw_script);
  REQUIRE(saw_tag);

  const auto path = std::filesystem::temp_directory_path() / "game_kit_scene_script.json";
  REQUIRE(game_kit::SaveSceneDocument(doc, path).ok());
  auto loaded = game_kit::LoadSceneDocument(path);
  REQUIRE(loaded.ok());
  engine::scene::World world2;
  std::unordered_map<std::string, engine::scene::NodeId> ids;
  REQUIRE(game_kit::ApplyWorld(world2, loaded.value(), &ids).ok());
  game_kit::GameRuntime rt2;
  game_kit::BindSceneScripts(rt2, world2, loaded.value(), ids);
  auto* e2 = rt2.entities().FindByName("chest");
  REQUIRE(e2 != nullptr);
  REQUIRE(e2->HasTag("loot"));
}

TEST_CASE("lua pcall isolates errors", "[gk2]") {
  game_kit::ScriptVm vm;
  if (!vm.available()) {
    return;
  }
  const auto st = vm.LoadString("error('boom')", "bad");
  REQUIRE(!st.ok());
  REQUIRE(vm.frozen());
  REQUIRE(vm.CallUpdate(0.016f).ok());
}

TEST_CASE("prefab instantiate and scene prefab_id roundtrip", "[gk4]") {
  const auto prefab = game_kit::MakeChestTagPrefab();
  REQUIRE(prefab.prefab_id == "chest_tag");
  engine::scene::World world;
  engine::scene::Transform t;
  t.position = {1.f, 2.f, 3.f};
  const auto root = game_kit::Instantiate(world, prefab, t);
  REQUIRE(world.valid(root));
  REQUIRE(world.local_transform(root).position.x == 1.f);
  REQUIRE(!world.roots().empty());

  game_kit::SceneDocument doc;
  game_kit::SceneNode n;
  n.id = "a";
  n.name = "inst";
  n.prefab_id = "chest_tag";
  n.script_path = "scripts/chest.lua";
  game_kit::SceneComponent script;
  script.type = "Script";
  script.script = "scripts/chest.lua";
  n.components.push_back(script);
  doc.nodes.push_back(n);
  doc.extensions_json = "{\"game_kit\":{}}";
  const auto path = std::filesystem::temp_directory_path() / "game_kit_prefab_scene.json";
  REQUIRE(game_kit::SaveSceneDocument(doc, path).ok());
  auto loaded = game_kit::LoadSceneDocument(path);
  REQUIRE(loaded.ok());
  REQUIRE(loaded.value().nodes.size() == 1);
  REQUIRE(loaded.value().nodes[0].prefab_id == "chest_tag");
  REQUIRE(loaded.value().nodes[0].script_path == "scripts/chest.lua");
  REQUIRE(loaded.value().extensions_json.find("game_kit") != std::string::npos);
  bool has_script = false;
  for (const auto& c : loaded.value().nodes[0].components) {
    if (c.type == "Script" && c.script == "scripts/chest.lua") {
      has_script = true;
    }
  }
  REQUIRE(has_script);
}

TEST_CASE("script hot reload polls mtime and reloads component", "[gk3][gk-g05]") {
  const auto dir = std::filesystem::temp_directory_path() / "game_kit_hot_reload";
  std::filesystem::create_directories(dir);
  const auto lua_path = dir / "tick.lua";
  {
    std::ofstream out(lua_path);
    out << "n = 0\nfunction on_update(dt) n = n + 1 end\n";
  }

  game_kit::ScriptComponentWorld scripts;
  const auto id = scripts.Attach(engine::scene::kInvalidNode, lua_path.string());
  auto* c = scripts.Get(id);
  REQUIRE(c != nullptr);
  if (!c->vm.available()) {
    return;
  }
  REQUIRE(scripts.LoadFromDisk(*c).ok());
  REQUIRE(scripts.Reload(*c).ok());
  REQUIRE(!c->vm.frozen());

  game_kit::ScriptHotReload hot;
  hot.WatchFile(lua_path);
  REQUIRE(!hot.Poll());  // baseline
  {
    // Ensure mtime advances on all platforms.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream out(lua_path);
    out << "n = 0\nfunction on_update(dt) n = n + 2 end\n";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const bool changed = hot.Poll();
  REQUIRE(changed);
  REQUIRE(!hot.changed_files().empty());
  REQUIRE(scripts.ReloadPath(lua_path.string()).ok());
  REQUIRE(!c->vm.frozen());
  REQUIRE(c->vm.CallUpdate(0.016f).ok());
}

TEST_CASE("coroutine scheduler skeleton ticks wake", "[gk5]") {
  game_kit::CoroutineScheduler sched;
  const auto h = sched.Spawn("wait");
  auto* slot = sched.Find(h);
  REQUIRE(slot != nullptr);
  slot->status = game_kit::CoroutineStatus::Suspended;
  slot->wake_after = 0.1f;
  sched.Tick(0.05f);
  REQUIRE(slot->status == game_kit::CoroutineStatus::Suspended);
  sched.Tick(0.1f);
  REQUIRE(slot->status == game_kit::CoroutineStatus::Pending);
}

TEST_CASE("ai state enum skeleton", "[gk5]") {
  game_kit::AiStateMachine ai;
  REQUIRE(ai.state == game_kit::AiState::Idle);
  ai.Set(game_kit::AiState::Chase);
  REQUIRE(ai.state == game_kit::AiState::Chase);
  REQUIRE(ai.previous == game_kit::AiState::Idle);
  REQUIRE(game_kit::ToString(ai.state) == "Chase");
}

TEST_CASE("IScriptHost null stub", "[gk5][c19]") {
  engine::script::NullScriptHost host;
  REQUIRE(host.RegisterFunction("log", [](int) { return 0; }).ok());
  host.Tick(0.016f);
  host.OnScriptError("none");
}

TEST_CASE("script vm hosts do not clobber each other", "[gk2]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto a = world.CreateNode("a");
  const auto b = world.CreateNode("b");
  rt.entities().Create("a", a);
  rt.entities().Create("b", b);
  rt.scripts().AttachHost(&world, &rt);

  const auto id1 = rt.scripts().Attach(a, "a.lua");
  auto* c1 = rt.scripts().Get(id1);
  REQUIRE(c1 != nullptr);
  if (!c1->vm.available()) {
    return;
  }
  REQUIRE(rt.scripts()
              .LoadFromString(*c1, "function on_update(dt) set_pos(1,2,3) end", "a")
              .ok());

  const auto id2 = rt.scripts().Attach(b, "b.lua");
  auto* c2 = rt.scripts().Get(id2);
  REQUIRE(c2 != nullptr);
  REQUIRE(rt.scripts()
              .LoadFromString(*c2, "function on_update(dt) set_pos(4,5,6) end", "b")
              .ok());

  rt.scripts().Tick(0.016f);
  REQUIRE(world.local_transform(a).position.x == 1.f);
  REQUIRE(world.local_transform(b).position.x == 4.f);
}

TEST_CASE("script on_init and on_destroy order", "[gk2]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  std::string life;
  rt.events().Subscribe("life", [&](std::string_view p) { life += std::string(p); });
  rt.scripts().AttachHost(&world, &rt);
  const auto n = world.CreateNode("s");
  const auto id = rt.scripts().Attach(n, "life.lua");
  auto* c = rt.scripts().Get(id);
  REQUIRE(c != nullptr);
  if (!c->vm.available()) {
    return;
  }
  REQUIRE(rt.scripts()
              .LoadFromString(*c,
                              "function on_init() publish('life','init') end\n"
                              "function on_destroy() publish('life','destroy') end\n",
                              "life")
              .ok());
  REQUIRE(life == "init");
  rt.scripts().Detach(id);
  REQUIRE(life == "initdestroy");
}

TEST_CASE("time_scale zero skips timers", "[gk1]") {
  game_kit::GameRuntime rt;
  int n = 0;
  rt.timer().Delay(0.01f, [&] { ++n; });
  rt.set_time_scale(0.f);
  rt.TickLogic(1.f);
  REQUIRE(n == 0);
  rt.set_time_scale(1.f);
  rt.TickLogic(1.f);
  REQUIRE(n == 1);
}

TEST_CASE("aabb trigger enter leave", "[gk3]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto player = world.CreateNode("player");
  const auto goal = world.CreateNode("goal");
  engine::scene::Transform tp;
  tp.position = {0.f, 0.f, 0.f};
  world.set_local_transform(player, tp);
  world.set_local_transform(goal, tp);
  rt.entities().Create("player", player);
  rt.scripts().AttachHost(&world, &rt);
  std::string got;
  rt.events().Subscribe("trigger.enter", [&](std::string_view p) { got = std::string(p); });
  const auto sid = rt.scripts().Attach(goal, "goal.lua");
  auto* sc = rt.scripts().Get(sid);
  REQUIRE(sc != nullptr);
  if (sc->vm.available()) {
    REQUIRE(rt.scripts()
                .LoadFromString(*sc,
                                "function on_trigger_enter(other) publish('script','enter') end",
                                "goal")
                .ok());
  }
  rt.triggers().Add("goal", goal, {1.f, 1.f, 1.f}, "player");
  std::string script_got;
  rt.events().Subscribe("script", [&](std::string_view p) { script_got = std::string(p); });
  rt.TickLogic(0.016f);
  REQUIRE(got.find("goal") != std::string::npos);
  if (sc->vm.available()) {
    REQUIRE(script_got == "enter");
  }
}

TEST_CASE("prefab instantiate attaches script component", "[gk4]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto dir = std::filesystem::temp_directory_path() / "game_kit_prefab_script";
  std::filesystem::create_directories(dir);
  const auto lua_path = dir / "chest.lua";
  {
    std::ofstream out(lua_path);
    out << "function on_init() end\n";
  }
  auto prefab = game_kit::MakeChestTagPrefab();
  prefab.scene.nodes[0].script_path = lua_path.string();
  for (auto& c : prefab.scene.nodes[0].components) {
    if (c.type == "Script") {
      c.script = lua_path.string();
    }
  }
  engine::scene::Transform t;
  t.position = {2.f, 0.f, 0.f};
  const auto root = game_kit::Instantiate(world, prefab, t, &rt);
  REQUIRE(world.valid(root));
  REQUIRE(!rt.scripts().all().empty());
  REQUIRE(rt.entities().FindByName("chest_tag") != nullptr);
}

TEST_CASE("lua wait coroutine resumes after delay", "[gk5]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  auto* vm = rt.script();
  REQUIRE(vm != nullptr);
  if (!vm->available()) {
    return;
  }
  vm->Attach(&world, &rt);
  std::string got;
  rt.events().Subscribe("co", [&](std::string_view p) { got = std::string(p); });
  REQUIRE(vm->LoadString(R"(
start_coroutine(function()
  wait(0.1)
  publish("co", "ok")
end)
)",
                         "co")
              .ok());
  rt.TickLogic(0.016f);
  REQUIRE(got.empty());
  rt.TickLogic(0.05f);
  REQUIRE(got.empty());
  rt.TickLogic(0.1f);
  REQUIRE(got == "ok");
}

TEST_CASE("raycast without physics misses", "[gk2]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  auto* vm = rt.script();
  if (!vm->available()) {
    return;
  }
  vm->Attach(&world, &rt);
  std::string got;
  rt.events().Subscribe("ray", [&](std::string_view p) { got = std::string(p); });
  REQUIRE(vm->LoadString(R"(
local hit = raycast(0,0,0, 0,1,0, 10)
publish("ray", hit and "hit" or "miss")
)",
                         "ray")
              .ok());
  REQUIRE(got == "miss");
}

TEST_CASE("GameKitScriptHost ticks and registers", "[gk5][c19]") {
  game_kit::GameRuntime rt;
  game_kit::GameKitScriptHost host(&rt);
  REQUIRE(host.RegisterFunction("log", [](int) { return 0; }).ok());
  REQUIRE(host.has("log"));
  host.Tick(0.016f);
  host.OnScriptError("none");
}
