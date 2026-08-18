#include "lua_host.h"

#include "game_kit/ai_state.h"
#include "game_kit/anim_player.h"
#include "game_kit/audio_mixer.h"
#include "game_kit/level_flow.h"
#include "game_kit/nav.h"
#include "game_kit/runtime.h"
#include "game_kit/script.h"
#include "game_kit/snapshot.h"
#include "game_kit/timeline.h"

#include "engine/app/application.h"
#include "engine/assets/asset_id.h"
#include "engine/assets/asset_system.h"
#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/input/input_system.h"
#include "engine/media/media.h"
#include "engine/physics/i_physics_world.h"
#include "engine/scene/world.h"
#include "engine/ui/retained_ui.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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

int LSetRot(lua_State* L) {
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
  const float yaw = static_cast<float>(luaL_checknumber(L, arg));
  const float pitch = static_cast<float>(luaL_optnumber(L, arg + 1, 0.0));
  const float roll = static_cast<float>(luaL_optnumber(L, arg + 2, 0.0));
  const auto node = NodeOf(h, name);
  if (node == engine::scene::kInvalidNode || !h->world->valid(node)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  auto t = h->world->local_transform(node);
  t.rotation = engine::Quat::FromEulerYxz(yaw, pitch, roll);
  h->world->set_local_transform(node, t);
  lua_pushboolean(L, 1);
  return 1;
}

int LGetRot(lua_State* L) {
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
  const auto& r = h->world->local_transform(node).rotation;
  lua_pushnumber(L, r.x);
  lua_pushnumber(L, r.y);
  lua_pushnumber(L, r.z);
  lua_pushnumber(L, r.w);
  return 4;
}

int LSetScale(lua_State* L) {
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
  const float y = static_cast<float>(luaL_optnumber(L, arg + 1, x));
  const float z = static_cast<float>(luaL_optnumber(L, arg + 2, x));
  const auto node = NodeOf(h, name);
  if (node == engine::scene::kInvalidNode || !h->world->valid(node)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  auto t = h->world->local_transform(node);
  t.scale = {x, y, z};
  h->world->set_local_transform(node, t);
  lua_pushboolean(L, 1);
  return 1;
}

int LGetScale(lua_State* L) {
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
  const auto& s = h->world->local_transform(node).scale;
  lua_pushnumber(L, s.x);
  lua_pushnumber(L, s.y);
  lua_pushnumber(L, s.z);
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
  const char* name = luaL_checkstring(L, 1);
  LoadMode mode = LoadMode::Replace;
  float delay = 0.f;
  if (lua_type(L, 2) == LUA_TSTRING) {
    const char* m = lua_tostring(L, 2);
    if (m && std::strcmp(m, "additive") == 0) {
      mode = LoadMode::Additive;
    }
    delay = static_cast<float>(luaL_optnumber(L, 3, 0.0));
  } else if (lua_type(L, 2) == LUA_TNUMBER) {
    delay = static_cast<float>(lua_tonumber(L, 2));
  }
  const auto st = h->rt->levels().Request(name, mode, delay);
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

int LUnsubscribe(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->events().Unsubscribe(static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
  }
  return 0;
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

engine::input::InputSystem* InputOf(LuaHost* h) {
  return (h && h->rt) ? h->rt->input() : nullptr;
}

int LKeyDown(lua_State* L) {
  auto* in = InputOf(Host(L));
  if (!in) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const auto key = ParseKey(luaL_checkstring(L, 1));
  lua_pushboolean(L, in->key_down(key) ? 1 : 0);
  return 1;
}

int LAxis(lua_State* L) {
  auto* in = InputOf(Host(L));
  if (!in) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, in->axis(luaL_checkstring(L, 1)));
  return 1;
}

int LPressed(lua_State* L) {
  auto* in = InputOf(Host(L));
  if (!in) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, in->pressed(luaL_checkstring(L, 1)) ? 1 : 0);
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
  const std::string id = luaL_checkstring(L, 1);
  GameRuntime* rt = h->rt;
  auto handle = h->rt->app()->assets().RequestLoad(
      engine::assets::AssetId(id), [rt, id](engine::Status st, engine::assets::AssetHandle) {
        if (rt) {
          rt->QueueAssetReady(id, st.ok());
        }
      });
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
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  std::string name;
  int arg = 1;
  if (lua_type(L, 1) == LUA_TSTRING && lua_gettop(L) >= 2 && lua_type(L, 2) == LUA_TSTRING) {
    name = lua_tostring(L, 1);
    arg = 2;
  }
  Entity* e = EntityOf(h, name);
  if (!e) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const char* state = luaL_checkstring(L, arg);
  const bool loop = lua_toboolean(L, arg + 1) != 0 || lua_isnoneornil(L, arg + 1);
  h->rt->anims().GetOrCreate(e->name).Play(state, loop);
  lua_pushboolean(L, 1);
  return 1;
}

int LStopAnim(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    return 0;
  }
  std::string name;
  if (lua_type(L, 1) == LUA_TSTRING) {
    name = lua_tostring(L, 1);
  }
  Entity* e = EntityOf(h, name);
  if (e) {
    h->rt->anims().GetOrCreate(e->name).Stop();
  }
  return 0;
}

int LAnimNotify(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  std::string name;
  int arg = 1;
  if (lua_type(L, 1) == LUA_TSTRING && lua_gettop(L) >= 4) {
    name = lua_tostring(L, 1);
    arg = 2;
  }
  Entity* e = EntityOf(h, name);
  if (!e) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, 1);
  return 1;
}

int LAnimTrigger(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
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
  h->rt->anims().GetOrCreate(e->name).SetTrigger(luaL_checkstring(L, arg));
  lua_pushboolean(L, 1);
  return 1;
}

int LImport(lua_State* L) {
  auto* h = Host(L);
  const char* mod = luaL_checkstring(L, 1);
  lua_getglobal(L, "_GK_LOADED");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "_GK_LOADED");
  }
  lua_getfield(L, -1, mod);
  if (!lua_isnil(L, -1)) {
    lua_remove(L, -2);
    return 1;
  }
  lua_pop(L, 1);

  std::string rel(mod);
  for (char& c : rel) {
    if (c == '.') {
      c = '/';
    }
  }
  if (rel.size() < 4 || rel.substr(rel.size() - 4) != ".lua") {
    rel += ".lua";
  }
  std::filesystem::path path = (h && h->rt) ? h->rt->ResolveScriptPath(rel) : std::filesystem::path(rel);
  if (luaL_loadfile(L, path.string().c_str()) != LUA_OK) {
    if (h && h->vm) {
      const char* err = lua_tostring(L, -1);
      h->vm->Freeze(err ? err : "import load");
    }
    return lua_error(L);
  }
  if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
    if (h && h->vm) {
      const char* err = lua_tostring(L, -1);
      h->vm->Freeze(err ? err : "import pcall");
    }
    return lua_error(L);
  }
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, mod);
  lua_remove(L, -2);
  return 1;
}

int LAddTag(lua_State* L) {
  auto* h = Host(L);
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
  e->AddTag(luaL_checkstring(L, arg));
  lua_pushboolean(L, 1);
  return 1;
}

int LHasTag(lua_State* L) {
  auto* h = Host(L);
  std::string name;
  int arg = 1;
  if (lua_type(L, 1) == LUA_TSTRING && lua_gettop(L) >= 2) {
    name = lua_tostring(L, 1);
    arg = 2;
  }
  Entity* e = EntityOf(h, name);
  lua_pushboolean(L, e && e->HasTag(luaL_checkstring(L, arg)) ? 1 : 0);
  return 1;
}

int LFindByTag(lua_State* L) {
  auto* h = Host(L);
  lua_newtable(L);
  if (!h || !h->rt) {
    return 1;
  }
  const char* tag = luaL_checkstring(L, 1);
  int i = 1;
  for (Entity* e : h->rt->entities().FindByTag(tag)) {
    lua_pushstring(L, e->name.c_str());
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

int LSteer(lua_State* L) {
  auto* h = Host(L);
  engine::Vec3 from{static_cast<float>(luaL_checknumber(L, 1)),
                    static_cast<float>(luaL_checknumber(L, 2)),
                    static_cast<float>(luaL_checknumber(L, 3))};
  engine::Vec3 goal{static_cast<float>(luaL_checknumber(L, 4)),
                    static_cast<float>(luaL_checknumber(L, 5)),
                    static_cast<float>(luaL_checknumber(L, 6))};
  const float speed = static_cast<float>(luaL_checknumber(L, 7));
  const float dt = static_cast<float>(luaL_checknumber(L, 8));
  engine::Vec3 n = from;
  if (h && h->rt) {
    n = h->rt->nav().Steer(from, goal, speed, dt);
  }
  lua_pushnumber(L, n.x);
  lua_pushnumber(L, n.y);
  lua_pushnumber(L, n.z);
  return 3;
}

int LAddNavObstacle(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->nav().AddObstacle(
        {static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)),
         static_cast<float>(luaL_checknumber(L, 3))},
        {static_cast<float>(luaL_optnumber(L, 4, 0.5)), static_cast<float>(luaL_optnumber(L, 5, 0.5)),
         static_cast<float>(luaL_optnumber(L, 6, 0.5))});
  }
  return 0;
}

int LSetSense(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->nav().SetSense(luaL_checkstring(L, 1), luaL_checkstring(L, 2),
                          static_cast<float>(luaL_checknumber(L, 3)));
  }
  return 0;
}

int LMixerPlay(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->mixer().Play(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""),
                      static_cast<float>(luaL_optnumber(L, 3, 1.0)), lua_toboolean(L, 4) != 0,
                      {static_cast<float>(luaL_optnumber(L, 5, 0)),
                       static_cast<float>(luaL_optnumber(L, 6, 0)),
                       static_cast<float>(luaL_optnumber(L, 7, 0))});
  lua_pushboolean(L, 1);
  return 1;
}

int LMixerStop(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
      h->rt->mixer().Stop(lua_tostring(L, 1));
    } else {
      h->rt->mixer().StopAll();
    }
  }
  return 0;
}

int LMixerGain(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    if (lua_type(L, 1) == LUA_TSTRING) {
      h->rt->mixer().set_bus_gain(lua_tostring(L, 1), static_cast<float>(luaL_checknumber(L, 2)));
    } else {
      h->rt->mixer().set_bus_gain(static_cast<float>(luaL_checknumber(L, 1)));
    }
  }
  return 0;
}

int LTimelineAdd(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->timeline().Add(static_cast<float>(luaL_checknumber(L, 1)), luaL_checkstring(L, 2),
                          luaL_optstring(L, 3, ""));
  }
  return 0;
}

int LTimelinePlay(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->timeline().Play();
  }
  return 0;
}

int LTimelineStop(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->timeline().Stop();
  }
  return 0;
}

int LTimelinePause(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->timeline().Pause();
  }
  return 0;
}

int LTimelineSeek(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->timeline().Seek(static_cast<float>(luaL_checknumber(L, 1)));
  }
  return 0;
}

int LSaveSnapshot(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const int slot = static_cast<int>(luaL_checkinteger(L, 1));
  const auto snap = CaptureSnapshot(*h->rt, h->world);
  lua_pushboolean(L, SaveSnapshot(h->rt->saves(), slot, snap).ok() ? 1 : 0);
  return 1;
}

int LLoadSnapshot(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  auto loaded = LoadSnapshot(h->rt->saves(), static_cast<int>(luaL_checkinteger(L, 1)));
  if (!loaded) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, ApplySnapshot(*h->rt, h->world, loaded.value()).ok() ? 1 : 0);
  return 1;
}

int LReplicatePush(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->replicator().Push(CaptureSnapshot(*h->rt, h->world));
  }
  return 0;
}

int LReplicatePull(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->replicator().has()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  lua_pushboolean(L, ApplySnapshot(*h->rt, h->world, h->rt->replicator().Pull()).ok() ? 1 : 0);
  return 1;
}

int LReplicatePushDiff(lua_State* L) {
  auto* h = Host(L);
  if (h && h->rt) {
    h->rt->replicator().PushDiff(CaptureSnapshot(*h->rt, h->world));
  }
  return 0;
}

int LUnloadLevel(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const auto st = h->rt->levels().UnloadStacked(luaL_checkstring(L, 1), h->rt->app(), *h->rt);
  lua_pushboolean(L, st.ok() ? 1 : 0);
  return 1;
}

int LSetPath(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    return 0;
  }
  const char* name = luaL_checkstring(L, 1);
  const float speed = static_cast<float>(luaL_optnumber(L, 2, 4.0));
  std::vector<engine::Vec3> pts;
  const int top = lua_gettop(L);
  for (int i = 3; i + 2 <= top; i += 3) {
    pts.push_back({static_cast<float>(luaL_checknumber(L, i)),
                   static_cast<float>(luaL_checknumber(L, i + 1)),
                   static_cast<float>(luaL_checknumber(L, i + 2))});
  }
  h->rt->nav().SetPath(name, std::move(pts), speed);
  return 0;
}

int LBakeNav(lua_State* L) {
  auto* h = Host(L);
  lua_pushboolean(L, (h && h->rt && h->rt->nav().BakeFromObstacles()) ? 1 : 0);
  return 1;
}

int LBakeNavWorld(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushboolean(L, 0);
    return 1;
  }
  bool ok = false;
  if (h->rt->physics()) {
    ok = h->rt->nav().BakeFromPhysics(*h->rt->physics());
  } else if (h->world) {
    ok = h->rt->nav().BakeFromWorld(*h->world);
  } else {
    ok = h->rt->nav().BakeFromObstacles();
  }
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int LNavAgent(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    lua_pushinteger(L, -1);
    return 1;
  }
  const char* name = luaL_checkstring(L, 1);
  const engine::Vec3 pos{static_cast<float>(luaL_optnumber(L, 2, 0.0)),
                         static_cast<float>(luaL_optnumber(L, 3, 0.0)),
                         static_cast<float>(luaL_optnumber(L, 4, 0.0))};
  const int idx = h->rt->nav().AddAgent(name, pos, static_cast<float>(luaL_optnumber(L, 5, 4.0)));
  if (lua_gettop(L) >= 8) {
    h->rt->nav().SetAgentTarget(name, {static_cast<float>(lua_tonumber(L, 6)),
                                       static_cast<float>(lua_tonumber(L, 7)),
                                       static_cast<float>(lua_tonumber(L, 8))});
  }
  lua_pushinteger(L, idx);
  return 1;
}

int LFindPath(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt) {
    return 0;
  }
  engine::Vec3 from{};
  if (h->world && h->self_node != engine::scene::kInvalidNode && h->world->valid(h->self_node)) {
    from = h->world->local_transform(h->self_node).position;
  }
  const engine::Vec3 to{static_cast<float>(luaL_checknumber(L, 2)),
                        static_cast<float>(luaL_checknumber(L, 3)),
                        static_cast<float>(luaL_checknumber(L, 4))};
  const char* name = luaL_checkstring(L, 1);
  auto pts = h->rt->nav().FindPath(from, to);
  h->rt->nav().SetPath(name, pts, static_cast<float>(luaL_optnumber(L, 5, 4.0)));
  lua_pushinteger(L, static_cast<lua_Integer>(pts.size()));
  return 1;
}

int LUiLabel(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->Label(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""),
                     static_cast<float>(luaL_optnumber(L, 3, 16.0)),
                     static_cast<float>(luaL_optnumber(L, 4, 16.0)));
  lua_pushboolean(L, 1);
  return 1;
}

int LUiButton(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->Button(luaL_checkstring(L, 1), luaL_optstring(L, 2, "OK"),
                      static_cast<float>(luaL_optnumber(L, 3, 16.0)),
                      static_cast<float>(luaL_optnumber(L, 4, 48.0)),
                      static_cast<float>(luaL_optnumber(L, 5, 80.0)),
                      static_cast<float>(luaL_optnumber(L, 6, 24.0)));
  lua_pushboolean(L, 1);
  return 1;
}

int LUiPanel(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->Panel(luaL_checkstring(L, 1), static_cast<float>(luaL_optnumber(L, 2, 16.0)),
                     static_cast<float>(luaL_optnumber(L, 3, 16.0)),
                     static_cast<float>(luaL_optnumber(L, 4, 280.0)),
                     static_cast<float>(luaL_optnumber(L, 5, 120.0)));
  lua_pushboolean(L, 1);
  return 1;
}

int LUiToggle(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->Toggle(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""),
                      static_cast<float>(luaL_optnumber(L, 3, 16.0)),
                      static_cast<float>(luaL_optnumber(L, 4, 16.0)),
                      static_cast<float>(luaL_optnumber(L, 5, 80.0)),
                      static_cast<float>(luaL_optnumber(L, 6, 22.0)), lua_toboolean(L, 7) != 0);
  lua_pushboolean(L, 1);
  return 1;
}

int LUiSlider(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->Slider(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""),
                      static_cast<float>(luaL_optnumber(L, 3, 16.0)),
                      static_cast<float>(luaL_optnumber(L, 4, 16.0)),
                      static_cast<float>(luaL_optnumber(L, 5, 160.0)),
                      static_cast<float>(luaL_optnumber(L, 6, 18.0)),
                      static_cast<float>(luaL_optnumber(L, 7, 0.0)),
                      static_cast<float>(luaL_optnumber(L, 8, 0.0)),
                      static_cast<float>(luaL_optnumber(L, 9, 1.0)));
  lua_pushboolean(L, 1);
  return 1;
}

int LUiLayout(lua_State* L) {
  auto* h = Host(L);
  if (!h || !h->rt || !h->rt->ui()) {
    lua_pushboolean(L, 0);
    return 1;
  }
  h->rt->ui()->LayoutColumn(luaL_checkstring(L, 1), static_cast<float>(luaL_optnumber(L, 2, 8.0)));
  lua_pushboolean(L, 1);
  return 1;
}

int LSetBudget(lua_State* L) {
  auto* h = Host(L);
  if (h && h->vm) {
    h->vm->set_instruction_budget(static_cast<int>(luaL_checkinteger(L, 1)));
  }
  return 0;
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

int LWaitEvent(lua_State* L) {
  auto* h = Host(L);
  const char* topic = luaL_checkstring(L, 1);
  if (lua_pushthread(L) == 1) {
    lua_pop(L, 1);
    return 0;
  }
  lua_pop(L, 1);
  if (h && h->rt) {
    if (auto* slot = h->rt->coroutines().FindByThread(L)) {
      slot->status = CoroutineStatus::Suspended;
      slot->wake_after = 0.f;
      slot->wake_topic = topic;
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
  auto* h = LuaHostOf(L);
  if (ar->event == LUA_HOOKCOUNT) {
    if (h && h->vm && h->vm->ConsumeBudget()) {
      luaL_error(L, "instruction budget exceeded");
    }
    return;
  }
  if (ar->event == LUA_HOOKLINE) {
    lua_getinfo(L, "Sl", ar);
    if (h && h->vm) {
      h->vm->NoteLine(ar->currentline, ar->short_src ? ar->short_src : "");
      h->vm->debugger().OnLine(L, ar->currentline, ar->short_src ? ar->short_src : "");
    }
    return;
  }
  if (ar->event != LUA_HOOKCALL) {
    return;
  }
  lua_getinfo(L, "n", ar);
  if (ar->name) {
    engine::LogInfo(std::string("lua call ") + ar->name);
  }
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
  lua_pushnil(L);
  lua_setglobal(L, "load");
  lua_pushnil(L);
  lua_setglobal(L, "loadfile");
  lua_pushnil(L);
  lua_setglobal(L, "dofile");

  static const luaL_Reg kApi[] = {
#include "lua_api_reg.inc"
  };
  lua_pushglobaltable(L);
  luaL_setfuncs(L, kApi, 0);
  lua_pop(L, 1);
}

void InstallLuaDebugHook(lua_State* L, bool enabled, int instruction_count) {
  if (!L) {
    return;
  }
  int mask = 0;
  int count = 0;
  if (enabled) {
    mask |= LUA_MASKCALL | LUA_MASKLINE;
  }
  if (instruction_count > 0) {
    mask |= LUA_MASKCOUNT;
    count = instruction_count;
  }
  if (mask == 0) {
    lua_sethook(L, nullptr, 0, 0);
  } else {
    lua_sethook(L, DebugHook, mask, count);
  }
}

#else

void LuaApiPlaceholder() {}

#endif
}  // namespace game_kit
