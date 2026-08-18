#include "game_kit/audio_mixer.h"

#include <cmath>

namespace game_kit {

void AudioMixer::set_bus_gain(std::string_view bus, float g) {
  buses_[std::string(bus)] = g < 0.f ? 0.f : g;
}

float AudioMixer::bus_gain(std::string_view bus) const {
  auto it = buses_.find(std::string(bus));
  return it == buses_.end() ? 1.f : it->second;
}

void AudioMixer::Play(std::string id, std::string path, float gain, bool spatial, engine::Vec3 pos,
                      std::string bus) {
  if (auto* v = Find(id)) {
    v->path = std::move(path);
    v->gain = gain;
    v->spatial = spatial;
    v->position = pos;
    v->bus = std::move(bus);
    v->playing = true;
    return;
  }
  AudioVoice v;
  v.id = std::move(id);
  v.path = std::move(path);
  v.gain = gain;
  v.spatial = spatial;
  v.position = pos;
  v.bus = std::move(bus);
  v.playing = true;
  voices_.push_back(std::move(v));
}

void AudioMixer::Stop(std::string_view id) {
  if (auto* v = Find(id)) {
    v->playing = false;
  }
}

void AudioMixer::StopAll() {
  for (auto& v : voices_) {
    v.playing = false;
  }
}

AudioVoice* AudioMixer::Find(std::string_view id) {
  for (auto& v : voices_) {
    if (v.id == id) {
      return &v;
    }
  }
  return nullptr;
}

void AudioMixer::Tick() {
  const float master = bus_gain("master");
  for (auto& v : voices_) {
    if (!v.playing) {
      v.last_output_gain = 0.f;
      continue;
    }
    float g = v.gain * master * bus_gain(v.bus);
    if (v.spatial) {
      const auto d = (v.position - listener_).length();
      g *= 1.f / (1.f + d);
    }
    v.last_output_gain = g;
  }
}

}  // namespace game_kit
