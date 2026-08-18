#include "engine/animation/state_machine.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace engine::animation {

void AnimationStateMachine::AddState(AnimState state) {
  if (state.name.empty()) {
    return;
  }
  for (auto& s : states_) {
    if (s.name == state.name) {
      s = std::move(state);
      return;
    }
  }
  states_.push_back(std::move(state));
  if (current_.empty()) {
    current_ = states_.back().name;
    state_time_ = 0.f;
  }
}

void AnimationStateMachine::AddTransition(AnimTransition transition) {
  transitions_.push_back(std::move(transition));
}

void AnimationStateMachine::SetDefaultCrossfadeDuration(float seconds) {
  default_crossfade_ = std::max(0.f, seconds);
}

float AnimationStateMachine::crossfade_alpha() const {
  if (!crossfading_ || crossfade_duration_ <= 1e-6f) {
    return 1.f;
  }
  return std::clamp(crossfade_t_ / crossfade_duration_, 0.f, 1.f);
}

void AnimationStateMachine::BeginCrossfade(std::string_view from, float from_time, float duration) {
  if (duration <= 1e-6f) {
    crossfading_ = false;
    return;
  }
  previous_ = std::string(from);
  previous_time_ = from_time;
  crossfade_duration_ = duration;
  crossfade_t_ = 0.f;
  crossfading_ = true;
}

void AnimationStateMachine::SetState(std::string_view name) {
  if (FindState(name) == nullptr) {
    return;
  }
  if (name == current_) {
    return;
  }
  const float fade = default_crossfade_;
  if (fade > 1e-6f && !current_.empty()) {
    BeginCrossfade(current_, state_time_, fade);
  } else {
    crossfading_ = false;
  }
  current_ = std::string(name);
  state_time_ = 0.f;
}

void AnimationStateMachine::SetTrigger(std::string_view name) {
  if (!name.empty()) {
    triggers_[std::string(name)] = true;
  }
}

void AnimationStateMachine::ClearTriggers() { triggers_.clear(); }

void AnimationStateMachine::AddNotify(std::string_view state, AnimNotify notify) {
  if (state.empty() || notify.name.empty()) {
    return;
  }
  notifies_[std::string(state)].push_back(std::move(notify));
}

std::span<const AnimNotify> AnimationStateMachine::NotifiesFor(std::string_view state) const {
  const auto it = notifies_.find(std::string(state));
  if (it == notifies_.end()) {
    return {};
  }
  return it->second;
}

std::span<const AnimNotifyEvent> AnimationStateMachine::DrainNotifies() { return fired_; }

const AnimState* AnimationStateMachine::FindState(std::string_view name) const {
  for (const auto& s : states_) {
    if (s.name == name) {
      return &s;
    }
  }
  return nullptr;
}

void AnimationStateMachine::CollectNotifiesCrossing(float prev_time, float curr_time) {
  const auto it = notifies_.find(current_);
  if (it == notifies_.end()) {
    return;
  }
  for (const auto& n : it->second) {
    if (n.time > prev_time && n.time <= curr_time) {
      fired_.push_back(AnimNotifyEvent{current_, n.name, n.time});
    }
  }
}

bool AnimationStateMachine::TryTransition() {
  const AnimState* cur = FindState(current_);
  if (!cur) {
    return false;
  }
  const float dur = std::max(cur->clip.duration, 1e-4f);
  const float norm = state_time_ / dur;
  for (const auto& t : transitions_) {
    if (t.from != current_) {
      continue;
    }
    if (FindState(t.to) == nullptr) {
      continue;
    }
    if (t.has_exit_time && norm < t.exit_time) {
      continue;
    }
    if (!t.trigger.empty()) {
      const auto it = triggers_.find(t.trigger);
      if (it == triggers_.end() || !it->second) {
        continue;
      }
      triggers_.erase(it);
    }
    const float fade =
        t.crossfade_duration > 1e-6f ? t.crossfade_duration : default_crossfade_;
    BeginCrossfade(current_, state_time_, fade);
    current_ = t.to;
    state_time_ = 0.f;
    return true;
  }
  return false;
}

void AnimationStateMachine::Update(float dt) {
  fired_.clear();
  if (dt < 0.f) {
    dt = 0.f;
  }
  const AnimState* cur = FindState(current_);
  if (!cur) {
    return;
  }

  if (crossfading_) {
    crossfade_t_ += dt;
    previous_time_ += dt;
    if (crossfade_t_ >= crossfade_duration_) {
      crossfading_ = false;
    }
  }

  const float prev = state_time_;
  state_time_ += dt;

  if (cur->loop && cur->clip.duration > 0.f && state_time_ > cur->clip.duration) {
    CollectNotifiesCrossing(prev, cur->clip.duration);
    state_time_ = std::fmod(state_time_, cur->clip.duration);
    if (state_time_ > 0.f) {
      CollectNotifiesCrossing(0.f, state_time_);
    }
  } else {
    CollectNotifiesCrossing(prev, state_time_);
  }

  if (TryTransition()) {
    return;
  }
  if (!cur->loop && cur->clip.duration > 0.f && state_time_ > cur->clip.duration) {
    state_time_ = cur->clip.duration;
  }
}

SkinPose AnimationStateMachine::LerpPoses(const SkinPose& a, const SkinPose& b, float t) {
  SkinPose out;
  const std::size_t n = std::max(a.bone_matrices.size(), b.bone_matrices.size());
  out.bone_matrices.resize(n, Mat4::Identity());
  const float u = std::clamp(t, 0.f, 1.f);
  for (std::size_t i = 0; i < n; ++i) {
    const Mat4& ma = i < a.bone_matrices.size() ? a.bone_matrices[i] : Mat4::Identity();
    const Mat4& mb = i < b.bone_matrices.size() ? b.bone_matrices[i] : Mat4::Identity();
    for (int k = 0; k < 16; ++k) {
      out.bone_matrices[i].m[k] = ma.m[k] + (mb.m[k] - ma.m[k]) * u;
    }
  }
  return out;
}

SkinPose AnimationStateMachine::Sample(const Skeleton& skel) const {
  const AnimState* cur = FindState(current_);
  if (!cur) {
    SkinPose empty;
    empty.bone_matrices.assign(skel.joints.size(), Mat4::Identity());
    return empty;
  }
  const SkinPose to_pose = SampleClip(skel, cur->clip, state_time_);
  if (!crossfading_) {
    return to_pose;
  }
  const AnimState* prev = FindState(previous_);
  if (!prev) {
    return to_pose;
  }
  const SkinPose from_pose = SampleClip(skel, prev->clip, previous_time_);
  return LerpPoses(from_pose, to_pose, crossfade_alpha());
}

SkinPose AnimationStateMachine::SampleBlend(const Skeleton& skel,
                                            std::span<const BlendLayer> layers) const {
  SkinPose out;
  out.bone_matrices.assign(skel.joints.size(), Mat4::Identity());
  for (auto& m : out.bone_matrices) {
    m.m.fill(0.f);
  }
  float wsum = 0.f;
  for (const auto& layer : layers) {
    if (layer.weight <= 0.f) {
      continue;
    }
    const AnimState* st = FindState(layer.state);
    if (!st) {
      continue;
    }
    const SkinPose pose = SampleClip(skel, st->clip, layer.time);
    const float w = layer.weight;
    wsum += w;
    for (std::size_t i = 0; i < out.bone_matrices.size() && i < pose.bone_matrices.size(); ++i) {
      for (int k = 0; k < 16; ++k) {
        out.bone_matrices[i].m[k] += pose.bone_matrices[i].m[k] * w;
      }
    }
  }
  if (wsum <= 1e-6f) {
    out.bone_matrices.assign(skel.joints.size(), Mat4::Identity());
    return out;
  }
  const float inv = 1.f / wsum;
  for (auto& m : out.bone_matrices) {
    for (int k = 0; k < 16; ++k) {
      m.m[k] *= inv;
    }
  }
  return out;
}

}  // namespace engine::animation
