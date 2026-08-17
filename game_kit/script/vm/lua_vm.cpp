#include "game_kit/script.h"

#include "game_kit/runtime.h"

#include "engine/core/log.h"
#include "engine/scene/world.h"

#include <fstream>
#include <memory>
#include <sstream>

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#endif

namespace game_kit {
namespace {

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA

struct LuaHost {
  engine::scene::World* world = nullptr;
  GameRuntime* rt = nullptr;
};

LuaHost& Host() {
  static LuaHost h;
  return h;
}

int LLog(lua_State* L) {
  engine::LogInfo(luaL_checkstring(L, 1));
  return 0;
}

int LSetPos(lua_State* L) {
  auto* world = Host().world;
  auto* rt = Host().rt;
  if (!world || !rt) {
    return 0;
  }
  const char* name = luaL_checkstring(L, 1);
  const float x = static_cast<float>(luaL_checknumber(L, 2));
  const float y = static_cast<float>(luaL_checknumber(L, 3));
  const float z = static_cast<float>(luaL_checknumber(L, 4));
  Entity* e = rt->entities().FindByName(name);
  if (!e || e->node == engine::scene::kInvalidNode || !world->valid(e->node)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  auto t = world->local_transform(e->node);
  t.position = {x, y, z};
  world->set_local_transform(e->node, t);
  lua_pushboolean(L, 1);
  return 1;
}

int LGetPos(lua_State* L) {
  auto* world = Host().world;
  auto* rt = Host().rt;
  if (!world || !rt) {
    return 0;
  }
  Entity* e = rt->entities().FindByName(luaL_checkstring(L, 1));
  if (!e || e->node == engine::scene::kInvalidNode || !world->valid(e->node)) {
    return 0;
  }
  const auto& t = world->local_transform(e->node);
  lua_pushnumber(L, t.position.x);
  lua_pushnumber(L, t.position.y);
  lua_pushnumber(L, t.position.z);
  return 3;
}

int LRequestLevel(lua_State* L) {
  auto* rt = Host().rt;
  if (!rt) {
    return 0;
  }
  const auto st = rt->levels().Request(luaL_checkstring(L, 1));
  lua_pushboolean(L, st.ok() ? 1 : 0);
  return 1;
}

int LPublish(lua_State* L) {
  auto* rt = Host().rt;
  if (!rt) {
    return 0;
  }
  rt->events().Publish(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
  return 0;
}

void OpenWhitelist(lua_State* L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  // No io / os / package / debug.

  lua_register(L, "log", LLog);
  lua_register(L, "set_pos", LSetPos);
  lua_register(L, "get_pos", LGetPos);
  lua_register(L, "request_level", LRequestLevel);
  lua_register(L, "publish", LPublish);
}

#endif

}  // namespace

struct ScriptVm::Impl {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  lua_State* L = nullptr;
  ~Impl() {
    if (L) {
      lua_close(L);
    }
  }
#endif
};

ScriptVm::ScriptVm() : impl_(std::make_unique<Impl>()) {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  impl_->L = luaL_newstate();
  if (impl_->L) {
    OpenWhitelist(impl_->L);
  }
#endif
}

ScriptVm::~ScriptVm() = default;

ScriptVm::ScriptVm(ScriptVm&&) noexcept = default;
ScriptVm& ScriptVm::operator=(ScriptVm&&) noexcept = default;

void ScriptVm::Attach(engine::scene::World* world, GameRuntime* rt) {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  Host().world = world;
  Host().rt = rt;
#else
  (void)world;
  (void)rt;
#endif
}

bool ScriptVm::available() const {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  return impl_ && impl_->L;
#else
  return false;
#endif
}

engine::Status ScriptVm::LoadString(std::string_view source, std::string_view chunk_name) {
  frozen_ = false;
  last_error_.clear();
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (!impl_->L) {
    return engine::Status::Fail("lua unavailable");
  }
  const std::string src(source);
  const std::string name(chunk_name.empty() ? "chunk" : chunk_name);
  if (luaL_loadbuffer(impl_->L, src.data(), src.size(), name.c_str()) != LUA_OK) {
    last_error_ = lua_tostring(impl_->L, -1) ? lua_tostring(impl_->L, -1) : "load error";
    lua_pop(impl_->L, 1);
    frozen_ = true;
    engine::LogError("lua load: " + last_error_);
    return engine::Status::Fail(last_error_);
  }
  if (lua_pcall(impl_->L, 0, 0, 0) != LUA_OK) {
    last_error_ = lua_tostring(impl_->L, -1) ? lua_tostring(impl_->L, -1) : "pcall error";
    lua_pop(impl_->L, 1);
    frozen_ = true;
    engine::LogError("lua pcall: " + last_error_);
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
    frozen_ = true;
    last_error_ = "cannot open " + std::string(path);
    return engine::Status::Fail(last_error_);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return LoadString(ss.str(), path);
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
  if (lua_pcall(impl_->L, 1, 0, 0) != LUA_OK) {
    last_error_ = lua_tostring(impl_->L, -1) ? lua_tostring(impl_->L, -1) : "on_update error";
    lua_pop(impl_->L, 1);
    frozen_ = true;
    engine::LogError("lua on_update frozen: " + last_error_);
    return engine::Status::Fail(last_error_);
  }
  return engine::Status::Ok();
#else
  (void)dt;
  return engine::Status::Ok();
#endif
}

void ScriptVm::Reset() {
  frozen_ = false;
  last_error_.clear();
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (impl_->L) {
    lua_close(impl_->L);
  }
  impl_->L = luaL_newstate();
  if (impl_->L) {
    OpenWhitelist(impl_->L);
  }
#endif
}

}  // namespace game_kit
