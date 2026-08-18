#pragma once

#include "engine/scene/world.h"

#include "game_kit/entity.h"

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
extern "C" {
#include "lua.h"
}
#endif

namespace game_kit {

class GameRuntime;
class ScriptVm;

struct LuaHost {
  engine::scene::World* world = nullptr;
  GameRuntime* rt = nullptr;
  ScriptVm* vm = nullptr;
  engine::scene::NodeId self_node = engine::scene::kInvalidNode;
  EntityId self_entity = kInvalidEntity;
};

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
inline LuaHost* LuaHostOf(lua_State* L) {
  if (!L) {
    return nullptr;
  }
  return *reinterpret_cast<LuaHost**>(lua_getextraspace(L));
}

inline void SetLuaHost(lua_State* L, LuaHost* host) {
  if (!L) {
    return;
  }
  *reinterpret_cast<LuaHost**>(lua_getextraspace(L)) = host;
}

void OpenLuaWhitelist(lua_State* L);
void InstallLuaDebugHook(lua_State* L, bool enabled, int instruction_count);
#endif

}  // namespace game_kit
