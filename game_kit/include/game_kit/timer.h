#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace game_kit {

class Timer {
 public:
  using Callback = std::function<void()>;

  std::uint32_t Delay(float seconds, Callback cb);
  std::uint32_t Interval(float seconds, Callback cb);
  void Cancel(std::uint32_t id);
  void Tick(float dt);
  void Clear();

  [[nodiscard]] std::size_t size() const { return items_.size(); }

 private:
  struct Item {
    std::uint32_t id = 0;
    float remaining = 0.f;
    float interval = 0.f;
    bool repeat = false;
    Callback cb;
  };
  std::vector<Item> items_;
  std::uint32_t next_id_ = 1;
};

}  // namespace game_kit
