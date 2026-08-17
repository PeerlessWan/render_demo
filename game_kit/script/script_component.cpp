#include "game_kit/script_component.h"

#include "game_kit/runtime.h"

#include "engine/core/log.h"

#include <fstream>
#include <sstream>

namespace game_kit {

ScriptComponentId ScriptComponentWorld::Attach(engine::scene::NodeId node, std::string path) {
  ScriptComponent c;
  c.id = next_id_++;
  c.node = node;
  c.path = std::move(path);
  const auto id = c.id;
  comps_.push_back(std::move(c));
  return id;
}

void ScriptComponentWorld::Detach(ScriptComponentId id) {
  for (auto it = comps_.begin(); it != comps_.end(); ++it) {
    if (it->id == id) {
      comps_.erase(it);
      return;
    }
  }
}

ScriptComponent* ScriptComponentWorld::Get(ScriptComponentId id) {
  for (auto& c : comps_) {
    if (c.id == id) {
      return &c;
    }
  }
  return nullptr;
}

engine::Status ScriptComponentWorld::LoadFromDisk(ScriptComponent& c) {
  if (c.path.empty()) {
    return engine::Status::Fail("empty script path");
  }
  std::ifstream in(c.path, std::ios::binary);
  if (!in) {
    return engine::Status::Fail("cannot open " + c.path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (world_ || rt_) {
    c.vm.Attach(world_, rt_);
  }
  return c.vm.LoadString(ss.str(), c.path);
}

engine::Status ScriptComponentWorld::Reload(ScriptComponent& c) {
  // Device-safe: only reset Lua state and re-load source.
  c.vm.Reset();
  if (world_ || rt_) {
    c.vm.Attach(world_, rt_);
  }
  auto st = LoadFromDisk(c);
  if (!st) {
    engine::LogError("script reload failed: " + c.path + " — " + st.message());
  } else {
    engine::LogInfo("script reloaded: " + c.path);
  }
  return st;
}

engine::Status ScriptComponentWorld::ReloadPath(std::string_view path) {
  engine::Status last = engine::Status::Ok();
  bool any = false;
  for (auto& c : comps_) {
    if (c.path == path) {
      any = true;
      last = Reload(c);
    }
  }
  if (!any) {
    return engine::Status::Fail("no ScriptComponent with path");
  }
  return last;
}

void ScriptComponentWorld::AttachHost(engine::scene::World* world, GameRuntime* rt) {
  world_ = world;
  rt_ = rt;
  for (auto& c : comps_) {
    c.vm.Attach(world_, rt_);
  }
}

void ScriptComponentWorld::Tick(float dt) {
  for (auto& c : comps_) {
    if (!c.enabled || c.vm.frozen()) {
      continue;
    }
    if (world_ || rt_) {
      c.vm.Attach(world_, rt_);
    }
    (void)c.vm.CallUpdate(dt);
  }
}

void ScriptComponentWorld::Clear() {
  comps_.clear();
  next_id_ = 1;
}

}  // namespace game_kit
