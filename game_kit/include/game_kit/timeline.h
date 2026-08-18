#pragma once

#include <string>
#include <vector>

namespace game_kit {

class EventBus;

struct TimelineCue {
  float time = 0.f;
  std::string topic;
  std::string payload;
  bool fired = false;
};

class Timeline {
 public:
  void Add(float time, std::string topic, std::string payload = {});
  void Play();
  void Pause();
  void Stop();
  void Seek(float time);
  void Tick(float dt, EventBus& events);
  void Clear();

  [[nodiscard]] bool playing() const { return playing_; }
  [[nodiscard]] float time() const { return time_; }
  [[nodiscard]] const std::vector<TimelineCue>& cues() const { return cues_; }

 private:
  std::vector<TimelineCue> cues_;
  float time_ = 0.f;
  bool playing_ = false;
};

}  // namespace game_kit
