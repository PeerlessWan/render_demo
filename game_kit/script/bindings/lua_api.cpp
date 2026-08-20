#include "lua_api_fn.h"
#include "lua_bind_util.h"
#include "lua_host.h"

#include "game_kit/anim_player.h"
#include "game_kit/audio_mixer.h"
#include "game_kit/level_flow.h"
#include "game_kit/nav.h"
#include "game_kit/runtime.h"
#include "game_kit/script.h"

#include "engine/app/application.h"
#include "engine/assets/asset_id.h"
#include "engine/assets/asset_system.h"
#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/media/media.h"
#include "engine/physics/i_physics_world.h"
#include "engine/scene/world.h"

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
  (void)arg;
  lua_pushboolean(L, 1);
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

namespace {

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
