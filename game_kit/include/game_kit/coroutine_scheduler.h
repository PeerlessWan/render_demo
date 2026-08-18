#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace game_kit {

class ScriptVm;

enum class CoroutineStatus : std::uint8_t {
  Pending = 0,
  Running,
  Suspended,
  Dead,
  Error,
};

struct CoroutineHandle {
  std::uint32_t id = 0;
};

struct CoroutineSlot {
  CoroutineHandle handle{};
  std::string name;
  CoroutineStatus status = CoroutineStatus::Pending;
  float wake_after = 0.f;  // seconds; 0 = ready
  ScriptVm* owner_vm = nullptr;
  void* lua_thread = nullptr;
  void* lua_main = nullptr;
  int registry_ref = -1;
};

class CoroutineScheduler {
 public:
  CoroutineHandle Spawn(std::string name);
  void Cancel(CoroutineHandle h);
  [[nodiscard]] CoroutineSlot* Find(CoroutineHandle h);
  [[nodiscard]] CoroutineSlot* FindByThread(void* lua_thread);
  [[nodiscard]] const std::vector<CoroutineSlot>& slots() const { return slots_; }
  [[nodiscard]] std::vector<CoroutineSlot>& slots() { return slots_; }

  // Advance wake timers; marks Suspended→Pending when due.
  void Tick(float dt);
  // lua_resume Pending slots that have a Lua thread.
  void ResumeLua();

  void Clear();

 private:
  std::vector<CoroutineSlot> slots_;
  std::uint32_t next_id_ = 1;
};

}  // namespace game_kit
