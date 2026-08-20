#pragma once

#include "lua_host.h"

#include "game_kit/entity.h"
#include "game_kit/runtime.h"

#include "engine/input/input_system.h"

#include <cstring>
#include <string>
#include <string_view>

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA

namespace game_kit {

inline LuaHost* Host(lua_State* L) { return LuaHostOf(L); }

inline engine::scene::NodeId NodeOf(LuaHost* h, std::string_view name) {
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

inline Entity* EntityOf(LuaHost* h, std::string_view name) {
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

inline engine::input::Key ParseKey(const char* s) {
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

inline engine::input::InputSystem* InputOf(LuaHost* h) {
  return (h && h->rt) ? h->rt->input() : nullptr;
}

}  // namespace game_kit

#endif
