#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

class EventBus {
 public:
  using Handler = std::function<void(std::string_view payload)>;

  std::uint32_t Subscribe(std::string topic, Handler handler);
  void Unsubscribe(std::uint32_t id);
  void Publish(std::string_view topic, std::string_view payload);
  void set_on_publish(std::function<void(std::string_view, std::string_view)> hook) {
    on_publish_ = std::move(hook);
  }
  void Clear();

 private:
  struct Sub {
    std::uint32_t id = 0;
    std::string topic;
    Handler handler;
  };
  std::vector<Sub> subs_;
  std::uint32_t next_id_ = 1;
  std::function<void(std::string_view, std::string_view)> on_publish_;
};

}  // namespace game_kit
