#include "game_kit/coroutine_scheduler.h"

namespace game_kit {

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

void CoroutineScheduler::Clear() {
  slots_.clear();
  next_id_ = 1;
}

}  // namespace game_kit
