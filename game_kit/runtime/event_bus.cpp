#include "game_kit/event_bus.h"

#include <algorithm>

namespace game_kit {

std::uint32_t EventBus::Subscribe(std::string topic, Handler handler) {
  Sub s;
  s.id = next_id_++;
  s.topic = std::move(topic);
  s.handler = std::move(handler);
  subs_.push_back(std::move(s));
  return subs_.back().id;
}

void EventBus::Unsubscribe(std::uint32_t id) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(), [id](const Sub& s) { return s.id == id; }),
              subs_.end());
}

void EventBus::Publish(std::string_view topic, std::string_view payload) {
  std::vector<Handler> copy;
  for (const auto& s : subs_) {
    if (s.topic == topic && s.handler) {
      copy.push_back(s.handler);
    }
  }
  for (auto& h : copy) {
    h(payload);
  }
}

void EventBus::Clear() { subs_.clear(); }

}  // namespace game_kit
