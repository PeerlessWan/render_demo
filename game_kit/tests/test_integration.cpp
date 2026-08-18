#include "game_kit/runtime.h"
#include "game_kit/script.h"
#include "game_kit/script_component.h"
#include "kit_test.h"

#include "engine/input/input_system.h"
#include "engine/scene/world.h"
#include "engine/ui/retained_ui.h"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("lua traceback includes line info", "[gk-script]") {
  game_kit::ScriptVm vm;
  if (!vm.available()) {
    return;
  }
  REQUIRE(vm.LoadString("function on_update(dt)\nerror('boom')\nend\n", "tb.lua").ok());
  REQUIRE(!vm.CallUpdate(0.016f).ok());
  REQUIRE(vm.frozen());
  REQUIRE(vm.last_traceback().find("tb.lua") != std::string::npos);
  REQUIRE(vm.last_traceback().find("boom") != std::string::npos);
}

TEST_CASE("lua import module cache", "[gk-script]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  auto* vm = rt.script();
  if (!vm || !vm->available()) {
    return;
  }
  const auto dir = std::filesystem::temp_directory_path() / "game_kit_import";
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "util.lua");
    out << "return { k = 9 }\n";
  }
  rt.set_script_root(dir);
  vm->Attach(&world, &rt);
  std::string got;
  rt.events().Subscribe("imp", [&](std::string_view p) { got = std::string(p); });
  REQUIRE(vm->LoadString("local u = import('util')\npublish('imp', tostring(u.k))\n", "imp").ok());
  REQUIRE(got == "9");
}

TEST_CASE("hot reload preserves persist table", "[gk-script]") {
  const auto dir = std::filesystem::temp_directory_path() / "game_kit_persist";
  std::filesystem::create_directories(dir);
  const auto lua_path = dir / "p.lua";
  {
    std::ofstream out(lua_path);
    out << "persist = persist or { n = 1 }\n"
           "function on_init() persist.n = persist.n or 1 end\n";
  }
  game_kit::ScriptComponentWorld scripts;
  const auto id = scripts.Attach(engine::scene::kInvalidNode, lua_path.string());
  auto* c = scripts.Get(id);
  REQUIRE(c != nullptr);
  if (!c->vm.available()) {
    return;
  }
  REQUIRE(scripts.LoadFromDisk(*c).ok());
  REQUIRE(c->vm.LoadString("persist.n = 42", "set").ok());
  {
    std::ofstream out(lua_path);
    out << "persist = persist or { n = 0 }\n"
           "function on_init() end\n"
           "function on_hot_reload() publish_n = persist.n end\n";
  }
  REQUIRE(scripts.Reload(*c, true).ok());
  REQUIRE(!c->vm.frozen());
  REQUIRE(c->vm.LoadString("if persist.n ~= 42 then error('persist lost') end", "chk").ok());
}

TEST_CASE("lua play_anim tag steer snapshot bindings", "[gk-script][gk-bind]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("hero");
  rt.entities().Create("hero", n);
  auto* vm = rt.script();
  if (!vm || !vm->available()) {
    return;
  }
  vm->Attach(&world, &rt);
  vm->BindSelf(n, rt.entities().FindByName("hero")->id);
  std::string got;
  rt.events().Subscribe("bind", [&](std::string_view p) { got += std::string(p); });
  REQUIRE(vm->LoadString(R"(
add_tag("hero")
publish("bind", has_tag("hero") and "t" or "f")
play_anim("walk", true)
local x,y,z = steer(0,0,0, 0,0,4, 2, 0.5)
publish("bind", z > 0 and "s" or "0")
mixer_play("v", "n.wav", 1, false)
timeline_add(0, "bind", "c")
timeline_play()
)",
                         "bind")
              .ok());
  rt.TickLogic(0.016f);
  REQUIRE(got.find('t') != std::string::npos);
  REQUIRE(got.find('s') != std::string::npos);
  REQUIRE(got.find('c') != std::string::npos);
  REQUIRE(rt.entities().FindByName("hero")->HasTag("hero"));
  REQUIRE(rt.anims().GetOrCreate("hero").current_state() == "walk");
}

TEST_CASE("asset ready queued into lua", "[gk-script]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  auto* vm = rt.script();
  if (!vm || !vm->available()) {
    return;
  }
  vm->Attach(&world, &rt);
  std::string got;
  rt.events().Subscribe("ar", [&](std::string_view p) { got = std::string(p); });
  REQUIRE(vm->LoadString("function on_asset_ready(id) publish('ar', id) end", "ar").ok());
  rt.QueueAssetReady("tex", true);
  rt.TickLogic(0.016f);
  REQUIRE(got == "tex");
}

TEST_CASE("mini level: trigger script mixer timeline", "[gk-int]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto player = world.CreateNode("player");
  const auto goal = world.CreateNode("goal");
  engine::scene::Transform t;
  world.set_local_transform(player, t);
  world.set_local_transform(goal, t);
  rt.entities().Create("player", player);
  rt.entities().Create("goal", goal);
  rt.scripts().AttachHost(&world, &rt);

  std::string done;
  rt.events().Subscribe("level.complete", [&](std::string_view p) { done = std::string(p); });
  const auto sid = rt.scripts().Attach(goal, "goal.lua");
  auto* sc = rt.scripts().Get(sid);
  REQUIRE(sc != nullptr);
  if (sc->vm.available()) {
    REQUIRE(rt.scripts()
                .LoadFromString(*sc,
                                "function on_trigger_enter(other) publish('level.complete', other) end",
                                "goal")
                .ok());
  }
  rt.triggers().Add("goal", goal, {1.f, 1.f, 1.f}, "player");
  rt.mixer().Play("amb", "a.wav", 0.5f);
  rt.timeline().Add(0.f, "level.complete", "cue");
  rt.TickLogic(0.016f);
  REQUIRE(!done.empty());
}

TEST_CASE("lua wait_event and rot scale", "[gk-script]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  const auto n = world.CreateNode("hero");
  rt.entities().Create("hero", n);
  auto* vm = rt.script();
  if (!vm || !vm->available()) {
    return;
  }
  vm->Attach(&world, &rt);
  vm->BindSelf(n, rt.entities().FindByName("hero")->id);
  std::string got;
  rt.events().Subscribe("co", [&](std::string_view p) { got = std::string(p); });
  REQUIRE(vm->LoadString(R"(
start_coroutine(function()
  wait_event("go")
  publish("co", "ok")
end)
set_rot(0.5, 0, 0)
set_scale(2, 2, 2)
)",
                         "we")
              .ok());
  rt.TickLogic(0.016f);
  REQUIRE(got.empty());
  rt.events().Publish("go", "");
  rt.TickLogic(0.016f);
  REQUIRE(got == "ok");
  REQUIRE(world.local_transform(n).scale.x > 1.5f);
}

TEST_CASE("lua instruction budget freezes infinite loop", "[gk-script]") {
  game_kit::ScriptVm vm;
  if (!vm.available()) {
    return;
  }
  vm.set_instruction_budget(200);
  REQUIRE(!vm.LoadString("while true do end", "loop").ok());
  REQUIRE(vm.frozen());
  REQUIRE(vm.last_error().find("budget") != std::string::npos);
}

TEST_CASE("lua input without application", "[gk-script]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  engine::input::InputSystem input;
  input.set_key(engine::input::Key::W, true);
  rt.set_input(&input);
  auto* vm = rt.script();
  if (!vm || !vm->available()) {
    return;
  }
  vm->Attach(&world, &rt);
  std::string got;
  rt.events().Subscribe("in", [&](std::string_view p) { got = std::string(p); });
  REQUIRE(vm->LoadString("publish('in', key_down('W') and 'w' or 'n')", "in").ok());
  REQUIRE(got == "w");
}

TEST_CASE("lua debug last_line", "[gk-script]") {
  game_kit::ScriptVm vm;
  if (!vm.available()) {
    return;
  }
  vm.set_debug_hooks(true);
  REQUIRE(vm.LoadString("function on_update(dt)\nlocal x = 1\nend\n", "ln.lua").ok());
  REQUIRE(vm.CallUpdate(0.016f).ok());
  REQUIRE(vm.last_line() >= 1);
  REQUIRE(vm.last_chunk().find("ln.lua") != std::string::npos);
}

TEST_CASE("lua sandbox strips load", "[gk-script]") {
  game_kit::ScriptVm vm;
  if (!vm.available()) {
    return;
  }
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  vm.Attach(&world, &rt);
  std::string got;
  rt.events().Subscribe("sb", [&](std::string_view p) { got = std::string(p); });
  REQUIRE(vm.LoadString("publish('sb', load == nil and 'ok' or 'bad')", "sb").ok());
  REQUIRE(got == "ok");
}

TEST_CASE("lua debugger breakpoint locals", "[gk-script]") {
  game_kit::ScriptVm vm;
  if (!vm.available()) {
    return;
  }
  vm.set_debug_hooks(true);
  bool hit = false;
  vm.debugger().AddBreakpoint("bp.lua", 2);
  vm.debugger().set_on_break([&](game_kit::ScriptDebugger& d) {
    hit = true;
    REQUIRE(d.line() == 2);
    REQUIRE(d.GetLocal("x").find("1") != std::string::npos);
  });
  REQUIRE(vm.LoadString("local x = 1\nlocal y = x + 1\n", "bp.lua").ok());
  REQUIRE(hit);
}

TEST_CASE("lua ui label binding without app", "[gk-script]") {
  engine::scene::World world;
  game_kit::GameRuntime rt;
  rt.set_world(&world);
  engine::ui::RetainedUi ui;
  ui.Label("msg", "hi", 0.f, 0.f);
  rt.set_ui(&ui);
  auto* vm = rt.script();
  if (!vm || !vm->available()) {
    return;
  }
  vm->Attach(&world, &rt);
  REQUIRE(vm->LoadString("ui_set_text('msg', 'go')", "ui").ok());
  REQUIRE(ui.widgets().size() >= 1);
}
