#include "game_kit/script.h"

#include "game_kit/runtime.h"
#include "lua_host.h"

#include "engine/core/log.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#endif

namespace game_kit {

struct ScriptVm::Impl {
  LuaHost host;
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  lua_State* L = nullptr;
  bool debug_hooks = false;
  ~Impl() {
    if (L) {
      lua_close(L);
    }
  }
#endif
};

ScriptVm::ScriptVm() : impl_(std::make_unique<Impl>()) {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  impl_->host.vm = this;
  impl_->L = luaL_newstate();
  if (impl_->L) {
    SetLuaHost(impl_->L, &impl_->host);
    OpenLuaWhitelist(impl_->L);
    InstallLuaDebugHook(impl_->L, impl_->debug_hooks);
  }
#endif
}

ScriptVm::~ScriptVm() = default;

ScriptVm::ScriptVm(ScriptVm&&) noexcept = default;
ScriptVm& ScriptVm::operator=(ScriptVm&&) noexcept = default;

void ScriptVm::Attach(engine::scene::World* world, GameRuntime* rt) {
  impl_->host.world = world;
  impl_->host.rt = rt;
  impl_->host.vm = this;
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (impl_->L) {
    SetLuaHost(impl_->L, &impl_->host);
  }
#endif
}

void ScriptVm::BindSelf(engine::scene::NodeId node, EntityId entity) {
  impl_->host.self_node = node;
  impl_->host.self_entity = entity;
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (impl_->L) {
    SetLuaHost(impl_->L, &impl_->host);
  }
#endif
}

void ScriptVm::set_debug_hooks(bool enabled) {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  impl_->debug_hooks = enabled;
  InstallLuaDebugHook(impl_->L, enabled);
#else
  (void)enabled;
#endif
}

bool ScriptVm::available() const {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  return impl_ && impl_->L;
#else
  return false;
#endif
}

void* ScriptVm::lua_state() const {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  return impl_ ? impl_->L : nullptr;
#else
  return nullptr;
#endif
}

void ScriptVm::Freeze(std::string_view message) {
  frozen_ = true;
  last_error_ = std::string(message);
  engine::LogError("lua frozen: " + last_error_);
}

engine::Status ScriptVm::LoadString(std::string_view source, std::string_view chunk_name) {
  frozen_ = false;
  last_error_.clear();
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (!impl_->L) {
    return engine::Status::Fail("lua unavailable");
  }
  SetLuaHost(impl_->L, &impl_->host);
  const std::string src(source);
  const std::string name(chunk_name.empty() ? "chunk" : chunk_name);
  if (luaL_loadbuffer(impl_->L, src.data(), src.size(), name.c_str()) != LUA_OK) {
    last_error_ = lua_tostring(impl_->L, -1) ? lua_tostring(impl_->L, -1) : "load error";
    lua_pop(impl_->L, 1);
    Freeze(last_error_);
    return engine::Status::Fail(last_error_);
  }
  if (lua_pcall(impl_->L, 0, 0, 0) != LUA_OK) {
    last_error_ = lua_tostring(impl_->L, -1) ? lua_tostring(impl_->L, -1) : "pcall error";
    lua_pop(impl_->L, 1);
    Freeze(last_error_);
    return engine::Status::Fail(last_error_);
  }
  return engine::Status::Ok();
#else
  (void)source;
  (void)chunk_name;
  return engine::Status::Fail("lua not compiled");
#endif
}

engine::Status ScriptVm::LoadFile(std::string_view path) {
  std::ifstream in(std::string(path), std::ios::binary);
  if (!in) {
    Freeze("cannot open " + std::string(path));
    return engine::Status::Fail(last_error_);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return LoadString(ss.str(), path);
}

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
namespace {
engine::Status CallGlobal(ScriptVm& vm, lua_State* L, const char* name, int nargs) {
  if (vm.frozen() || !L) {
    if (nargs > 0) {
      lua_pop(L, nargs);
    }
    return engine::Status::Ok();
  }
  if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
    const std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "pcall error";
    lua_pop(L, 1);
    vm.Freeze(std::string(name) + ": " + err);
    return engine::Status::Fail(vm.last_error());
  }
  return engine::Status::Ok();
}
}  // namespace
#endif

engine::Status ScriptVm::CallNamed(std::string_view name) {
  if (frozen_ || !available()) {
    return engine::Status::Ok();
  }
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  const std::string n(name);
  lua_getglobal(impl_->L, n.c_str());
  if (!lua_isfunction(impl_->L, -1)) {
    lua_pop(impl_->L, 1);
    return engine::Status::Ok();
  }
  return CallGlobal(*this, impl_->L, n.c_str(), 0);
#else
  (void)name;
  return engine::Status::Ok();
#endif
}

engine::Status ScriptVm::CallUpdate(float dt) {
  if (frozen_ || !available()) {
    return engine::Status::Ok();
  }
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  lua_getglobal(impl_->L, "on_update");
  if (!lua_isfunction(impl_->L, -1)) {
    lua_pop(impl_->L, 1);
    return engine::Status::Ok();
  }
  lua_pushnumber(impl_->L, dt);
  return CallGlobal(*this, impl_->L, "on_update", 1);
#else
  (void)dt;
  return engine::Status::Ok();
#endif
}

engine::Status ScriptVm::CallTrigger(bool enter, std::string_view other) {
  if (frozen_ || !available()) {
    return engine::Status::Ok();
  }
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  const char* name = enter ? "on_trigger_enter" : "on_trigger_leave";
  lua_getglobal(impl_->L, name);
  if (!lua_isfunction(impl_->L, -1)) {
    lua_pop(impl_->L, 1);
    return engine::Status::Ok();
  }
  lua_pushlstring(impl_->L, other.data(), other.size());
  return CallGlobal(*this, impl_->L, name, 1);
#else
  (void)enter;
  (void)other;
  return engine::Status::Ok();
#endif
}

void ScriptVm::Reset() {
  frozen_ = false;
  last_error_.clear();
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (impl_->L) {
    lua_close(impl_->L);
    impl_->L = nullptr;
  }
  impl_->L = luaL_newstate();
  impl_->host.vm = this;
  if (impl_->L) {
    SetLuaHost(impl_->L, &impl_->host);
    OpenLuaWhitelist(impl_->L);
    InstallLuaDebugHook(impl_->L, impl_->debug_hooks);
  }
#endif
}

}  // namespace game_kit
