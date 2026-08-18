#include "game_kit/timeline.h"

#include "game_kit/event_bus.h"

namespace game_kit {

void Timeline::Add(float time, std::string topic, std::string payload) {
  TimelineCue c;
  c.time = time;
  c.topic = std::move(topic);
  c.payload = std::move(payload);
  cues_.push_back(std::move(c));
}

void Timeline::Play() {
  playing_ = true;
  time_ = 0.f;
  for (auto& c : cues_) {
    c.fired = false;
  }
}

void Timeline::Pause() { playing_ = false; }

void Timeline::Stop() { playing_ = false; }

void Timeline::Seek(float t) {
  time_ = t < 0.f ? 0.f : t;
  for (auto& c : cues_) {
    c.fired = c.time <= time_;
  }
}

void Timeline::Tick(float dt, EventBus& events) {
  if (!playing_) {
    return;
  }
  time_ += dt;
  for (auto& c : cues_) {
    if (!c.fired && time_ >= c.time) {
      c.fired = true;
      events.Publish(c.topic, c.payload);
    }
  }
}

void Timeline::Clear() {
  cues_.clear();
  time_ = 0.f;
  playing_ = false;
}

}  // namespace game_kit
