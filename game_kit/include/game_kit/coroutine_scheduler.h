#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace game_kit {

// GK5 thin: Lua coroutine scheduler skeleton (no full yield/resume bindings yet).
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
};

class CoroutineScheduler {
 public:
  CoroutineHandle Spawn(std::string name);
  void Cancel(CoroutineHandle h);
  [[nodiscard]] CoroutineSlot* Find(CoroutineHandle h);
  [[nodiscard]] const std::vector<CoroutineSlot>& slots() const { return slots_; }

  // Advance wake timers; marks Suspended→Pending when due. Does not call into Lua yet.
  void Tick(float dt);

  void Clear();

 private:
  std::vector<CoroutineSlot> slots_;
  std::uint32_t next_id_ = 1;
};

}  // namespace game_kit
