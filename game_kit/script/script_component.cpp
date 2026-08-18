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
      CallDestroy(*it);
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

ScriptComponent* ScriptComponentWorld::FindByNode(engine::scene::NodeId node) {
  for (auto& c : comps_) {
    if (c.node == node) {
      return &c;
    }
  }
  return nullptr;
}

void ScriptComponentWorld::Bind(ScriptComponent& c) {
  if (world_ || rt_) {
    c.vm.Attach(world_, rt_);
  }
  EntityId eid = c.entity;
  if (eid == kInvalidEntity && rt_) {
    if (auto* e = rt_->entities().FindByNode(c.node)) {
      eid = e->id;
      c.entity = eid;
    }
  }
  c.vm.BindSelf(c.node, eid);
  if (rt_) {
    c.vm.set_debug_hooks(rt_->script_debug());
  }
}

void ScriptComponentWorld::CallDestroy(ScriptComponent& c) {
  if (!c.inited_) {
    return;
  }
  Bind(c);
  (void)c.vm.CallNamed("on_destroy");
  c.inited_ = false;
}

engine::Status ScriptComponentWorld::LoadFromString(ScriptComponent& c, std::string_view source,
                                                    std::string_view chunk_name) {
  Bind(c);
  auto st = c.vm.LoadString(source, chunk_name);
  if (!st) {
    return st;
  }
  st = c.vm.CallNamed("on_init");
  c.inited_ = st.ok();
  return st;
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
  return LoadFromString(c, ss.str(), c.path);
}

engine::Status ScriptComponentWorld::Reload(ScriptComponent& c) {
  CallDestroy(c);
  c.vm.Reset();
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
    Bind(c);
  }
}

void ScriptComponentWorld::Tick(float dt) {
  for (auto& c : comps_) {
    if (!c.enabled || c.vm.frozen()) {
      continue;
    }
    Bind(c);
    (void)c.vm.CallUpdate(dt);
  }
}

void ScriptComponentWorld::DispatchTrigger(engine::scene::NodeId node, bool enter,
                                           std::string_view other) {
  for (auto& c : comps_) {
    if (c.node != node || !c.enabled || c.vm.frozen()) {
      continue;
    }
    Bind(c);
    (void)c.vm.CallTrigger(enter, other);
  }
}

void ScriptComponentWorld::Clear() {
  for (auto& c : comps_) {
    CallDestroy(c);
  }
  comps_.clear();
  next_id_ = 1;
}

}  // namespace game_kit
