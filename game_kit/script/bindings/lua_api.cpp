#include "lua_host.h"

#include "game_kit/ai_state.h"
#include "game_kit/runtime.h"
#include "game_kit/script.h"

#include "engine/app/application.h"
#include "engine/assets/asset_id.h"
#include "engine/assets/asset_system.h"
#include "engine/core/log.h"
#include "engine/input/input_system.h"
#include "engine/media/media.h"
#include "engine/physics/i_physics_world.h"
#include "engine/scene/world.h"
#include "engine/ui/retained_ui.h"

#include <cstring>
#include <string>
#include <string_view>

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#endif

namespace game_kit {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
namespace {

LuaHost* Host(lua_State* L) { return LuaHostOf(L); }

engine::scene::NodeId NodeOf(LuaHost* h, std::string_view name) {
  if (!h || !h->rt || !h->world) {
    return engine::scene::kInvalidNode;
  }
  if (!name.empty()) {
    if (Entity* e = h->rt->entities().FindByName(name)) {
      return e->node;
    }
    return engine::scene::kInvalidNode;
  }
  return h->self_node;
}

Entity* EntityOf(LuaHost* h, std::string_view name) {
  if (!h || !h->rt) {
    return nullptr;
  }
  if (!name.empty()) {
    return h->rt->entities().FindByName(name);
  }
  if (h->self_entity != kInvalidEntity) {
    return h->rt->entities().Get(h->self_entity);
  }
  if (h->self_node != engine::scene::kInvalidNode) {
    return h->rt->entities().FindByNode(h->self_node);
  }
  return nullptr;
}

int LLog(lua_State* L) {
  engine::LogInfo(luaL_checkstring(L, 1));
  return 0;
}

int LSetPos(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->world) {
    lua_pushboolean(L, 0);
    return 1;
  }
  int arg = 1;
  std::string name;
  if (lua_type(L, 1) == LUA_TSTRING) {
    name = lua_tostring(L, 1);
    arg = 2;
  }
  const float x = static_cast<float>(luaL_checknumber(L, arg));
  const float y = static_cast<float>(luaL_checknumber(L, arg + 1));
  const float z = static_cast<float>(luaL_checknumber(L, arg + 2));
  const auto node = NodeOf(h, name);
  if (node == engine::scene::kInvalidNode || !h->world->valid(node)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  auto t = h->world->local_transform(node);
  t.position = {x, y, z};
  h->world->set_local_transform(node, t);
  lua_pushboolean(L, 1);
  return 1;
}

int LGetPos(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->world) {
    return 0;
  }
  std::string name;
  if (lua_type(L, 1) == LUA_TSTRING) {
    name = lua_tostring(L, 1);
  }
  const auto node = NodeOf(h, name);
  if (node == engine::scene::kInvalidNode || !h->world->valid(node)) {
    return 0;
  }
  const auto& t = h->world->local_transform(node);
  lua_pushnumber(L, t.position.x);
  lua_pushnumber(L, t.position.y);
  lua_pushnumber(L, t.position.z);
  return 3;
}

int LSetVisible(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->world) {
    lua_pushboolean(L, 0);
    return 1;
  }
  int arg = 1;
  std::string name;
  if (lua_type(L, 1) == LUA_TSTRING && lua_type(L, 2) != LUA_TNONE && lua_gettop(L) >= 2 &&
      (lua_type(L, 2) == LUA_TBOOLEAN || lua_type(L, 2) == LUA_TNUMBER)) {
    name = lua_tostring(L, 1);
    arg = 2;
  }
  const bool vis = lua_toboolean(L, arg) != 0;
  const auto node = NodeOf(h, name);
  if (node == engine::scene::kInvalidNode || !h->world->valid(node)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->world->set_visible(node, vis);
  lua_pushboolean(L, 1);
  return 1;
}

int LGetChildren(lua_State* L) {
  auto* h = Host(L);
  lua_newtable(L);
  if (!h || !h->world) {
    return 1;
  }
  std::string name;
  if (lua_type(L, 1) == LUA_TSTRING) {
    name = lua_tostring(L, 1);
  }
  const auto node = NodeOf(h, name);
  if (node == engine::scene::kInvalidNode || !h->world->valid(node)) {
    return 1;
  }
  int i = 1;
  for (auto c : h->world->children(node)) {
    lua_pushstring(L, h->world->name(c).c_str());
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

int LDestroyEntity(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  std::string name;
  if (lua_type(L, 1) == LUA_TSTRING) {
    name = lua_tostring(L, 1);
  }
  Entity* e = EntityOf(h, name);
  if (!e) {
    lua_pushboolean(L, 0);
    return 1;
  }
  if (h->world && e->node != engine::scene::kInvalidNode && h->world->valid(e->node)) {
    (void)h->world->DestroyNode(e->node);
  }
  h->rt->entities().Destroy(e->id);
  lua_pushboolean(L, 1);
  return 1;
}

int LInstantiate(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const char* id = luaL_checkstring(L, 1);
  engine::scene::Transform t;
  t.position.x = static_cast<float>(luaL_optnumber(L, 2, 0.0));
  t.position.y = static_cast<float>(luaL_optnumber(L, 3, 0.0));
  t.position.z = static_cast<float>(luaL_optnumber(L, 4, 0.0));
  const auto node = h->rt->SpawnPrefab(id, t);
  lua_pushboolean(L, node != engine::scene::kInvalidNode);
  return 1;
}

int LRequestLevel(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const auto st = h->rt->levels().Request(luaL_checkstring(L, 1));
  lua_pushboolean(L, st.ok() ? 1 : 0);
  return 1;
}

int LCurrentLevel(lua_State* L) {
  auto* h = Host(L);
  lua_pushstring(L, (h && h->rt) ? h->rt->levels().current().c_str() : "");
  return 1;
}

int LPublish(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    return 0;
  }
  h->rt->events().Publish(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""));
  return 0;
}

int LDelay(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->vm) {
    lua_pushinteger(L, 0);
    return 1;
  }
  const float seconds = static_cast<float>(luaL_checknumber(L, 1));
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_State* main = static_cast<lua_State*>(h->vm->lua_state());
  ScriptVm* vm = h->vm;
  const auto id = h->rt->timer().Delay(seconds, [main, ref, vm]() {
    if (!main || !vm || vm->frozen()) {
      return;
    }
    lua_rawgeti(main, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(main, 0, 0, 0) != LUA_OK) {
      const char* err = lua_tostring(main, -1);
      vm->Freeze(err ? err : "delay callback");
      lua_pop(main, 1);
    }
    luaL_unref(main, LUA_REGISTRYINDEX, ref);
  });
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

int LInterval(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->vm) {
    lua_pushinteger(L, 0);
    return 1;
  }
  const float seconds = static_cast<float>(luaL_checknumber(L, 1));
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_State* main = static_cast<lua_State*>(h->vm->lua_state());
  ScriptVm* vm = h->vm;
  const auto id = h->rt->timer().Interval(seconds, [main, ref, vm]() {
    if (!main || !vm || vm->frozen()) {
      return;
    }
    lua_rawgeti(main, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(main, 0, 0, 0) != LUA_OK) {
      const char* err = lua_tostring(main, -1);
      vm->Freeze(err ? err : "interval callback");
      lua_pop(main, 1);
    }
  });
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

int LCancelTimer(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->timer().Cancel(static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
  }
  return 0;
}

int LSubscribe(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->vm) {
    lua_pushinteger(L, 0);
    return 1;
  }
  const char* topic = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_State* main = static_cast<lua_State*>(h->vm->lua_state());
  ScriptVm* vm = h->vm;
  const auto id = h->rt->events().Subscribe(topic, [main, ref, vm](std::string_view payload) {
    if (!main || !vm || vm->frozen()) {
      return;
    }
    lua_rawgeti(main, LUA_REGISTRYINDEX, ref);
    lua_pushlstring(main, payload.data(), payload.size());
    if (lua_pcall(main, 1, 0, 0) != LUA_OK) {
      const char* err = lua_tostring(main, -1);
      vm->Freeze(err ? err : "subscribe callback");
      lua_pop(main, 1);
    }
  });
  lua_pushinteger(L, static_cast<lua_Integer>(id));
  return 1;
}

int LSetPaused(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->set_paused(lua_toboolean(L, 1) != 0);
  }
  return 0;
}

int LSetTimeScale(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->set_time_scale(static_cast<float>(luaL_checknumber(L, 1)));
  }
  return 0;
}

int LGetTimeScale(lua_State* L) {
  auto* h = Host(L);
  lua_pushnumber(L, (h && h->rt) ? h->rt->time_scale() : 1.0);
  return 1;
}

engine::input::Key ParseKey(const char* s) {
  if (!s) {
    return engine::input::Key::Unknown;
  }
  if (std::strcmp(s, "W") == 0 || std::strcmp(s, "w") == 0) {
    return engine::input::Key::W;
  }
  if (std::strcmp(s, "A") == 0 || std::strcmp(s, "a") == 0) {
    return engine::input::Key::A;
  }
  if (std::strcmp(s, "S") == 0 || std::strcmp(s, "s") == 0) {
    return engine::input::Key::S;
  }
  if (std::strcmp(s, "D") == 0 || std::strcmp(s, "d") == 0) {
    return engine::input::Key::D;
  }
  if (std::strcmp(s, "Q") == 0 || std::strcmp(s, "q") == 0) {
    return engine::input::Key::Q;
  }
  if (std::strcmp(s, "E") == 0 || std::strcmp(s, "e") == 0) {
    return engine::input::Key::E;
  }
  if (std::strcmp(s, "Space") == 0 || std::strcmp(s, "space") == 0) {
    return engine::input::Key::Space;
  }
  if (std::strcmp(s, "Escape") == 0 || std::strcmp(s, "escape") == 0) {
    return engine::input::Key::Escape;
  }
  return engine::input::Key::Unknown;
}

int LKeyDown(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->app()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const auto key = ParseKey(luaL_checkstring(L, 1));
  lua_pushboolean(L, h->rt->app()->input().key_down(key) ? 1 : 0);
  return 1;
}

int LAxis(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->app()) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, h->rt->app()->input().axis(luaL_checkstring(L, 1)));
  return 1;
}

int LPressed(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->app()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, h->rt->app()->input().pressed(luaL_checkstring(L, 1)) ? 1 : 0);
  return 1;
}

int LRaycast(lua_State* L) {
  auto* h = Host(L);
  lua_pushboolean(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushinteger(L, -1);
  if (!h || !h->rt || !h->rt->physics()) {
    return 6;
  }
  engine::Vec3 origin{static_cast<float>(luaL_checknumber(L, 1)),
                      static_cast<float>(luaL_checknumber(L, 2)),
                      static_cast<float>(luaL_checknumber(L, 3))};
  engine::Vec3 dir{static_cast<float>(luaL_checknumber(L, 4)),
                   static_cast<float>(luaL_checknumber(L, 5)),
                   static_cast<float>(luaL_checknumber(L, 6))};
  const float max_d = static_cast<float>(luaL_optnumber(L, 7, 100.0));
  const auto hit = h->rt->physics()->Raycast(origin, dir, max_d);
  lua_pushboolean(L, hit.hit ? 1 : 0);
  lua_replace(L, -7);
  lua_pushnumber(L, hit.point.x);
  lua_replace(L, -6);
  lua_pushnumber(L, hit.point.y);
  lua_replace(L, -5);
  lua_pushnumber(L, hit.point.z);
  lua_replace(L, -4);
  lua_pushnumber(L, hit.distance);
  lua_replace(L, -3);
  lua_pushinteger(L, hit.body_id);
  lua_replace(L, -2);
  return 6;
}

int LUiSetText(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->set_text(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
  lua_pushboolean(L, 1);
  return 1;
}

int LUiSetVisible(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->set_visible(luaL_checkstring(L, 1), lua_toboolean(L, 2) != 0);
  lua_pushboolean(L, 1);
  return 1;
}

int LPlayWav(lua_State* L) {
  auto* h = Host(L);
  const char* path = luaL_checkstring(L, 1);
  if (h && h->rt && h->rt->audio()) {
    auto clip = engine::media::LoadWavPcm16(path);
    if (!clip) {
      lua_pushboolean(L, 0);
      return 1;
    }
    const auto st = h->rt->audio()->Play(clip.value(), static_cast<float>(luaL_optnumber(L, 2, 1.0)));
    lua_pushboolean(L, st.ok() ? 1 : 0);
    return 1;
  }
  const auto st = engine::media::PlayWavFile(path);
  lua_pushboolean(L, st.ok() ? 1 : 0);
  return 1;
}

int LStopAudio(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt && h->rt->audio()) {
    h->rt->audio()->StopAll();
    lua_pushboolean(L, 1);
    return 1;
  }
  lua_pushboolean(L, 0);
  return 1;
}

int LRequestLoad(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->app()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const char* id = luaL_checkstring(L, 1);
  auto handle = h->rt->app()->assets().RequestLoad(engine::assets::AssetId(id));
  h->rt->RememberAsset(id, handle);
  lua_pushboolean(L, 1);
  return 1;
}

int LAssetReady(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const auto* handle = h->rt->FindAsset(luaL_checkstring(L, 1));
  lua_pushboolean(L, handle && handle->is_ready() ? 1 : 0);
  return 1;
}

int LSetAi(lua_State* L) {
  auto* h = Host(L);
  if (!h) {
    lua_pushboolean(L, 0);
    return 1;
  }
  std::string name;
  int arg = 1;
  if (lua_type(L, 1) == LUA_TSTRING && lua_gettop(L) >= 2) {
    name = lua_tostring(L, 1);
    arg = 2;
  }
  Entity* e = EntityOf(h, name);
  if (!e) {
    lua_pushboolean(L, 0);
    return 1;
  }
  e->ai.Set(ParseAiState(luaL_checkstring(L, arg)));
  lua_pushboolean(L, 1);
  return 1;
}

int LGetAi(lua_State* L) {
  auto* h = Host(L);
  std::string name;
  if (lua_type(L, 1) == LUA_TSTRING) {
    name = lua_tostring(L, 1);
  }
  Entity* e = EntityOf(h, name);
  lua_pushstring(L, e ? ToString(e->ai.state).data() : "Idle");
  return 1;
}

int LPlayAnim(lua_State* L) {
  (void)L;
  lua_pushboolean(L, 0);
  return 1;
}

int LWait(lua_State* L) {
  auto* h = Host(L);
  const float seconds = static_cast<float>(luaL_checknumber(L, 1));
  if (lua_pushthread(L) == 1) {
    lua_pop(L, 1);
    return 0;
  }
  lua_pop(L, 1);
  if (h && h->rt) {
    if (auto* slot = h->rt->coroutines().FindByThread(L)) {
      slot->status = CoroutineStatus::Suspended;
      slot->wake_after = seconds < 0.f ? 0.f : seconds;
    }
  }
  return lua_yield(L, 0);
}

int LStartCoroutine(lua_State* L) {
  auto* h = Host(L);
  luaL_checktype(L, 1, LUA_TFUNCTION);
  if (!h || !h->rt || !h->vm) {
    lua_pushinteger(L, 0);
    return 1;
  }
  lua_State* co = lua_newthread(L);
  SetLuaHost(co, h);
  lua_pushvalue(L, 1);
  lua_xmove(L, co, 1);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  auto handle = h->rt->coroutines().Spawn("lua");
  if (auto* slot = h->rt->coroutines().Find(handle)) {
    slot->owner_vm = h->vm;
    slot->lua_thread = co;
    slot->lua_main = h->vm->lua_state();
    slot->registry_ref = ref;
    slot->status = CoroutineStatus::Pending;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(handle.id));
  return 1;
}

void DebugHook(lua_State* L, lua_Debug* ar) {
  if (ar->event != LUA_HOOKCALL) {
    return;
  }
  lua_getinfo(L, "n", ar);
  engine::LogInfo(std::string("lua call ") + (ar->name ? ar->name : "?"));
}

}  // namespace

void OpenLuaWhitelist(lua_State* L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_pop(L, 1);

  lua_register(L, "log", LLog);
  lua_register(L, "set_pos", LSetPos);
  lua_register(L, "get_pos", LGetPos);
  lua_register(L, "set_visible", LSetVisible);
  lua_register(L, "get_children", LGetChildren);
  lua_register(L, "destroy_entity", LDestroyEntity);
  lua_register(L, "instantiate", LInstantiate);
  lua_register(L, "request_level", LRequestLevel);
  lua_register(L, "current_level", LCurrentLevel);
  lua_register(L, "publish", LPublish);
  lua_register(L, "delay", LDelay);
  lua_register(L, "interval", LInterval);
  lua_register(L, "cancel_timer", LCancelTimer);
  lua_register(L, "subscribe", LSubscribe);
  lua_register(L, "set_paused", LSetPaused);
  lua_register(L, "set_time_scale", LSetTimeScale);
  lua_register(L, "get_time_scale", LGetTimeScale);
  lua_register(L, "key_down", LKeyDown);
  lua_register(L, "axis", LAxis);
  lua_register(L, "pressed", LPressed);
  lua_register(L, "raycast", LRaycast);
  lua_register(L, "ui_set_text", LUiSetText);
  lua_register(L, "ui_set_visible", LUiSetVisible);
  lua_register(L, "play_wav", LPlayWav);
  lua_register(L, "stop_audio", LStopAudio);
  lua_register(L, "request_load", LRequestLoad);
  lua_register(L, "asset_ready", LAssetReady);
  lua_register(L, "set_ai", LSetAi);
  lua_register(L, "get_ai", LGetAi);
  lua_register(L, "play_anim", LPlayAnim);
  lua_register(L, "wait", LWait);
  lua_register(L, "start_coroutine", LStartCoroutine);
}

void InstallLuaDebugHook(lua_State* L, bool enabled) {
  if (!L) {
    return;
  }
  if (enabled) {
    lua_sethook(L, DebugHook, LUA_MASKCALL, 0);
  } else {
    lua_sethook(L, nullptr, 0, 0);
  }
}

#else

void LuaApiPlaceholder() {}

#endif
}  // namespace game_kit
