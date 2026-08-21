#include "engine/render2d/animation_player2d.h"

#include <algorithm>
#include <cmath>

namespace engine::render2d {
namespace {

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

AnimKey2D EvalKeys(const std::vector<AnimKey2D>& keys, float t, bool lerp_f) {
  if (keys.empty()) {
    return {};
  }
  if (t <= keys.front().time) {
    return keys.front();
  }
  if (t >= keys.back().time) {
    return keys.back();
  }
  for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
    if (t >= keys[i].time && t <= keys[i + 1].time) {
      const float span = std::max(1e-6f, keys[i + 1].time - keys[i].time);
      const float u = (t - keys[i].time) / span;
      AnimKey2D o = keys[i];
      if (lerp_f) {
        o.f0 = Lerp(keys[i].f0, keys[i + 1].f0, u);
        o.f1 = Lerp(keys[i].f1, keys[i + 1].f1, u);
        o.f2 = Lerp(keys[i].f2, keys[i + 1].f2, u);
        o.f3 = Lerp(keys[i].f3, keys[i + 1].f3, u);
      } else {
        o = (u < 1.f) ? keys[i] : keys[i + 1];
      }
      o.time = t;
      return o;
    }
  }
  return keys.back();
}

}  // namespace

void AnimationPlayer2D::AddClip(AnimationClip2D clip) {
  const std::string name = clip.name;
  clips_[name] = std::move(clip);
}

bool AnimationPlayer2D::HasClip(const std::string& name) const { return clips_.count(name) > 0; }

bool AnimationPlayer2D::Play(const std::string& name) {
  if (!HasClip(name)) {
    return false;
  }
  active_ = name;
  time_ = 0.f;
  playing_ = true;
  return true;
}

void AnimationPlayer2D::Stop() {
  playing_ = false;
  time_ = 0.f;
}

void AnimationPlayer2D::Step(float dt) {
  if (!playing_ || active_.empty()) {
    return;
  }
  auto it = clips_.find(active_);
  if (it == clips_.end()) {
    playing_ = false;
    return;
  }
  time_ += dt;
  if (time_ > it->second.length) {
    if (it->second.loop) {
      time_ = std::fmod(time_, std::max(1e-4f, it->second.length));
    } else {
      time_ = it->second.length;
      playing_ = false;
    }
  }
}

void AnimationPlayer2D::SampleActive(std::vector<Sample>* out) const {
  if (!out || active_.empty()) {
    return;
  }
  out->clear();
  auto it = clips_.find(active_);
  if (it == clips_.end()) {
    return;
  }
  std::unordered_map<int, Sample> by_item;
  for (const auto& tr : it->second.tracks) {
    Sample& s = by_item[tr.target_item];
    s.item = tr.target_item;
    const bool lerp = tr.type != AnimTrackType2D::SpriteFrame;
    const AnimKey2D k = EvalKeys(tr.keys, time_, lerp);
    switch (tr.type) {
      case AnimTrackType2D::Position:
        s.has_pos = true;
        s.position = {k.f0, k.f1};
        break;
      case AnimTrackType2D::Rotation:
        s.has_rot = true;
        s.rotation = k.f0;
        break;
      case AnimTrackType2D::Modulate:
        s.has_mod = true;
        s.modulate = {k.f0, k.f1, k.f2, k.f3};
        break;
      case AnimTrackType2D::SpriteFrame:
        s.has_frame = true;
        s.frame = k.i0;
        break;
    }
  }
  out->reserve(by_item.size());
  for (auto& kv : by_item) {
    out->push_back(kv.second);
  }
}

}  // namespace engine::render2d
