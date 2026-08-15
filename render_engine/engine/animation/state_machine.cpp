#include "engine/animation/state_machine.h"

#include <algorithm>

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

void AnimationStateMachine::SetState(std::string_view name) {
  if (FindState(name) == nullptr) {
    return;
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

const AnimState* AnimationStateMachine::FindState(std::string_view name) const {
  for (const auto& s : states_) {
    if (s.name == name) {
      return &s;
    }
  }
  return nullptr;
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
    current_ = t.to;
    state_time_ = 0.f;
    return true;
  }
  return false;
}

void AnimationStateMachine::Update(float dt) {
  if (dt < 0.f) {
    dt = 0.f;
  }
  const AnimState* cur = FindState(current_);
  if (!cur) {
    return;
  }
  state_time_ += dt;
  if (TryTransition()) {
    return;
  }
  if (!cur->loop && cur->clip.duration > 0.f && state_time_ > cur->clip.duration) {
    state_time_ = cur->clip.duration;
  }
}

SkinPose AnimationStateMachine::Sample(const Skeleton& skel) const {
  const AnimState* cur = FindState(current_);
  if (!cur) {
    SkinPose empty;
    empty.bone_matrices.assign(skel.joints.size(), Mat4::Identity());
    return empty;
  }
  return SampleClip(skel, cur->clip, state_time_);
}

}  // namespace engine::animation
