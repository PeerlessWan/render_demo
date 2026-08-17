#include "game_kit/timer.h"

#include <algorithm>

namespace game_kit {

std::uint32_t Timer::Delay(float seconds, Callback cb) {
  Item item;
  item.id = next_id_++;
  item.remaining = seconds;
  item.cb = std::move(cb);
  items_.push_back(std::move(item));
  return items_.back().id;
}

std::uint32_t Timer::Interval(float seconds, Callback cb) {
  Item item;
  item.id = next_id_++;
  item.remaining = seconds;
  item.interval = seconds;
  item.repeat = true;
  item.cb = std::move(cb);
  items_.push_back(std::move(item));
  return items_.back().id;
}

void Timer::Cancel(std::uint32_t id) {
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [id](const Item& i) { return i.id == id; }),
               items_.end());
}

void Timer::Tick(float dt) {
  if (dt <= 0.f) {
    return;
  }
  std::vector<Callback> fire;
  for (auto it = items_.begin(); it != items_.end();) {
    it->remaining -= dt;
    if (it->remaining > 0.f) {
      ++it;
      continue;
    }
    if (it->cb) {
      fire.push_back(it->cb);
    }
    if (it->repeat) {
      it->remaining += it->interval;
      if (it->remaining <= 0.f) {
        it->remaining = it->interval;
      }
      ++it;
    } else {
      it = items_.erase(it);
    }
  }
  for (auto& cb : fire) {
    cb();
  }
}

void Timer::Clear() { items_.clear(); }

}  // namespace game_kit
