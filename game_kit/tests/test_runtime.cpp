#include "game_kit/entity.h"
#include "game_kit/event_bus.h"
#include "game_kit/level_flow.h"
#include "game_kit/prefab.h"
#include "game_kit/save.h"
#include "game_kit/scene_document.h"
#include "game_kit/script.h"
#include "game_kit/script_component.h"
#include "game_kit/script_hot_reload.h"
#include "game_kit/coroutine_scheduler.h"
#include "game_kit/ai_state.h"
#include "game_kit/timer.h"
#include "kit_test.h"

#include "engine/scene/world.h"
#include "engine/script/i_script_host.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

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
  REQUIRE(loaded.value().format_version == 1);
  REQUIRE(!loaded.value().nodes.empty());

  engine::scene::World world2;
  REQUIRE(game_kit::ApplyWorld(world2, loaded.value()).ok());
  REQUIRE(!world2.roots().empty());
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
