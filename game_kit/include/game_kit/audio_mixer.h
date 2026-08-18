#pragma once

#include "engine/core/math.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_kit {

struct AudioVoice {
  std::string id;
  std::string path;
  std::string bus = "sfx";
  float gain = 1.f;
  engine::Vec3 position{};
  bool spatial = false;
  bool playing = true;
  float last_output_gain = 1.f;
};

class AudioMixer {
 public:
  void set_bus_gain(float g) { set_bus_gain("master", g); }
  void set_bus_gain(std::string_view bus, float g);
  [[nodiscard]] float bus_gain() const { return bus_gain("master"); }
  [[nodiscard]] float bus_gain(std::string_view bus) const;
  void set_listener(engine::Vec3 p) { listener_ = p; }
  [[nodiscard]] const engine::Vec3& listener() const { return listener_; }

  void Play(std::string id, std::string path, float gain = 1.f, bool spatial = false,
            engine::Vec3 pos = {}, std::string bus = "sfx");
  void Stop(std::string_view id);
  void StopAll();
  void Tick();

  [[nodiscard]] AudioVoice* Find(std::string_view id);
  [[nodiscard]] const std::vector<AudioVoice>& voices() const { return voices_; }

 private:
  std::vector<AudioVoice> voices_;
  std::unordered_map<std::string, float> buses_{{"master", 1.f}, {"sfx", 1.f}, {"music", 1.f}};
  engine::Vec3 listener_{};
};

}  // namespace game_kit
