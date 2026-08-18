#include "game_kit/coroutine_scheduler.h"

#include "game_kit/script.h"

#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
extern "C" {
#include "lauxlib.h"
#include "lua.h"
}
#endif

namespace game_kit {
namespace {

void UnrefSlot(CoroutineSlot& s) {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  if (s.lua_main && s.registry_ref >= 0) {
    luaL_unref(static_cast<lua_State*>(s.lua_main), LUA_REGISTRYINDEX, s.registry_ref);
  }
#endif
  s.registry_ref = -2;
  s.lua_thread = nullptr;
  s.lua_main = nullptr;
}

}  // namespace

CoroutineHandle CoroutineScheduler::Spawn(std::string name) {
  CoroutineSlot slot;
  slot.handle.id = next_id_++;
  slot.name = std::move(name);
  slot.status = CoroutineStatus::Pending;
  const auto h = slot.handle;
  slots_.push_back(std::move(slot));
  return h;
}

void CoroutineScheduler::Cancel(CoroutineHandle h) {
  for (auto it = slots_.begin(); it != slots_.end(); ++it) {
    if (it->handle.id == h.id) {
      UnrefSlot(*it);
      slots_.erase(it);
      return;
    }
  }
}

CoroutineSlot* CoroutineScheduler::Find(CoroutineHandle h) {
  for (auto& s : slots_) {
    if (s.handle.id == h.id) {
      return &s;
    }
  }
  return nullptr;
}

CoroutineSlot* CoroutineScheduler::FindByThread(void* lua_thread) {
  if (!lua_thread) {
    return nullptr;
  }
  for (auto& s : slots_) {
    if (s.lua_thread == lua_thread) {
      return &s;
    }
  }
  return nullptr;
}

void CoroutineScheduler::Tick(float dt) {
  if (dt < 0.f) {
    dt = 0.f;
  }
  for (auto& s : slots_) {
    if (s.status == CoroutineStatus::Dead || s.status == CoroutineStatus::Error) {
      continue;
    }
    if (s.status == CoroutineStatus::Suspended && s.wake_after > 0.f) {
      s.wake_after -= dt;
      if (s.wake_after <= 0.f) {
        s.wake_after = 0.f;
        s.status = CoroutineStatus::Pending;
      }
    }
  }
}

void CoroutineScheduler::ResumeLua() {
#if defined(GAME_KIT_WITH_LUA) && GAME_KIT_WITH_LUA
  for (auto& s : slots_) {
    if (s.status != CoroutineStatus::Pending || !s.lua_thread) {
      continue;
    }
    s.status = CoroutineStatus::Running;
    auto* co = static_cast<lua_State*>(s.lua_thread);
    auto* main = static_cast<lua_State*>(s.lua_main);
    int nres = 0;
    const int rc = lua_resume(co, main, 0, &nres);
    if (rc == LUA_YIELD) {
      if (s.status == CoroutineStatus::Running) {
        s.status = CoroutineStatus::Suspended;
      }
      if (nres > 0) {
        lua_pop(co, nres);
      }
    } else if (rc == LUA_OK) {
      s.status = CoroutineStatus::Dead;
      if (nres > 0) {
        lua_pop(co, nres);
      }
      UnrefSlot(s);
    } else {
      s.status = CoroutineStatus::Error;
      const char* err = lua_tostring(co, -1);
      if (s.owner_vm) {
        s.owner_vm->Freeze(err ? err : "coroutine error");
      }
      if (nres > 0 || err) {
        lua_pop(co, 1);
      }
      UnrefSlot(s);
    }
  }
#endif
}

void CoroutineScheduler::Clear() {
  for (auto& s : slots_) {
    UnrefSlot(s);
  }
  slots_.clear();
  next_id_ = 1;
}

}  // namespace game_kit
