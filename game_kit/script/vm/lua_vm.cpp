#include "game_kit/script.h"

#include "game_kit/runtime.h"
#include "lua_host.h"

#include "engine/core/log.h"

#include <cstdlib>
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

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
namespace {
int MsgHandler(lua_State* L) {
  const char* msg = lua_tostring(L, 1);
  luaL_traceback(L, L, msg ? msg : "", 1);
  return 1;
}

engine::Status PCall(ScriptVm& vm, lua_State* L, int nargs, const char* ctx) {
  if (vm.frozen() || !L) {
    if (L && nargs > 0) {
      lua_pop(L, nargs);
    }
    return engine::Status::Ok();
  }
  const int msgh = lua_gettop(L) - nargs;
  lua_pushcfunction(L, MsgHandler);
  lua_insert(L, msgh);
  const int rc = lua_pcall(L, nargs, 0, msgh);
  lua_remove(L, msgh);
  if (rc != LUA_OK) {
    const std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "pcall error";
    lua_pop(L, 1);
    vm.Freeze(std::string(ctx) + ": " + err);
    return engine::Status::Fail(vm.last_error());
  }
  return engine::Status::Ok();
}
}  // namespace
#endif

ScriptVm::ScriptVm() : impl_(std::make_unique<Impl>()) {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  impl_->host.vm = this;
  impl_->L = luaL_newstate();
  if (impl_->L) {
    SetLuaHost(impl_->L, &impl_->host);
    OpenLuaWhitelist(impl_->L);
    RefreshHooks();
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
  RefreshHooks();
#else
  (void)enabled;
#endif
}

void ScriptVm::set_instruction_budget(int n) {
  instruction_budget_ = n < 0 ? 0 : n;
  RefreshHooks();
}

void ScriptVm::NoteLine(int line, std::string_view chunk) {
  last_line_ = line;
  last_chunk_ = std::string(chunk);
}

bool ScriptVm::ConsumeBudget() {
  if (instruction_budget_ <= 0) {
    return false;
  }
  --instruction_budget_;
  if (instruction_budget_ > 0) {
    return false;
  }
  Freeze("instruction budget exceeded");
  return true;
}

void ScriptVm::RefreshHooks() {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (impl_ && impl_->L) {
    InstallLuaDebugHook(impl_->L, impl_->debug_hooks, instruction_budget_ > 0 ? 1 : 0);
  }
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
  last_traceback_ = last_error_;
  engine::LogError("lua frozen: " + last_error_);
}

engine::Status ScriptVm::LoadString(std::string_view source, std::string_view chunk_name) {
  frozen_ = false;
  last_error_.clear();
  last_traceback_.clear();
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
  return PCall(*this, impl_->L, 0, name.c_str());
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
  return PCall(*this, impl_->L, 0, n.c_str());
#else
  (void)name;
  return engine::Status::Ok();
#endif
}

engine::Status ScriptVm::CallNamed1(std::string_view name, std::string_view arg) {
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
  lua_pushlstring(impl_->L, arg.data(), arg.size());
  return PCall(*this, impl_->L, 1, n.c_str());
#else
  (void)name;
  (void)arg;
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
  return PCall(*this, impl_->L, 1, "on_update");
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
  return PCall(*this, impl_->L, 1, name);
#else
  (void)enter;
  (void)other;
  return engine::Status::Ok();
#endif
}

std::string ScriptVm::DumpPersist() const {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (!impl_ || !impl_->L) {
    return {};
  }
  lua_State* L = impl_->L;
  lua_getglobal(L, "persist");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return {};
  }
  std::ostringstream ss;
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING && (lua_isnumber(L, -1) || lua_isstring(L, -1))) {
      ss << lua_tostring(L, -2) << '=' << lua_tostring(L, -1) << '\n';
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return ss.str();
#else
  return {};
#endif
}

engine::Status ScriptVm::RestorePersist(std::string_view blob) {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (!impl_ || !impl_->L) {
    return engine::Status::Fail("lua unavailable");
  }
  lua_State* L = impl_->L;
  lua_newtable(L);
  std::size_t i = 0;
  while (i < blob.size()) {
    const auto nl = blob.find('\n', i);
    const auto line = blob.substr(i, (nl == std::string_view::npos ? blob.size() : nl) - i);
    i = (nl == std::string_view::npos) ? blob.size() : nl + 1;
    const auto eq = line.find('=');
    if (eq == std::string_view::npos || eq == 0) {
      continue;
    }
    const auto key = line.substr(0, eq);
    const auto val = line.substr(eq + 1);
    lua_pushlstring(L, key.data(), key.size());
    char* end = nullptr;
    const std::string vs(val);
    const float n = std::strtof(vs.c_str(), &end);
    if (end && end != vs.c_str() && *end == '\0') {
      lua_pushnumber(L, n);
    } else {
      lua_pushlstring(L, val.data(), val.size());
    }
    lua_rawset(L, -3);
  }
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING) {
      const char* key = lua_tostring(L, -2);
      lua_pushvalue(L, -1);
      lua_setglobal(L, key);
    }
    lua_pop(L, 1);
  }
  lua_setglobal(L, "persist");
  return engine::Status::Ok();
#else
  (void)blob;
  return engine::Status::Ok();
#endif
}

void ScriptVm::Reset() {
  frozen_ = false;
  last_error_.clear();
  last_traceback_.clear();
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
    RefreshHooks();
  }
#endif
}

void ScriptDebugger::AddBreakpoint(std::string chunk, int line) {
  bps_.push_back(Bp{std::move(chunk), line});
}

std::string ScriptDebugger::GetLocal(std::string_view name) const {
  for (const auto& kv : locals_) {
    if (kv.first == name) {
      return kv.second;
    }
  }
  return {};
}

void ScriptDebugger::OnLine(void* lua_state, int line, std::string_view src) {
  line_ = line;
  chunk_ = std::string(src);
  bool hit = step_;
  step_ = false;
  for (const auto& bp : bps_) {
    if (line == bp.line && (bp.chunk.empty() || chunk_.find(bp.chunk) != std::string::npos)) {
      hit = true;
      break;
    }
  }
  if (!hit) {
    return;
  }
  locals_.clear();
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  auto* L = static_cast<lua_State*>(lua_state);
  if (L) {
    lua_Debug ar{};
    if (lua_getstack(L, 0, &ar) && lua_getinfo(L, "nSl", &ar)) {
      for (int i = 1;; ++i) {
        const char* name = lua_getlocal(L, &ar, i);
        if (!name) {
          break;
        }
        std::string val = lua_tostring(L, -1) ? lua_tostring(L, -1) : luaL_typename(L, -1);
        locals_.push_back({name, std::move(val)});
        lua_pop(L, 1);
      }
    }
  }
#else
  (void)lua_state;
#endif
  if (on_break_) {
    waiting_ = true;
    on_break_(*this);
    waiting_ = false;
  }
}

}  // namespace game_kit
