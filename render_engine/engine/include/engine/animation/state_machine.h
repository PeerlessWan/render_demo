#pragma once

#include "engine/animation/skeleton.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::animation {

struct AnimState {
  std::string name;
  AnimationClip clip;
  bool loop = true;
};

struct AnimTransition {
  std::string from;
  std::string to;
  // Normalized [0,1] of clip duration; transition eligible when time_norm >= exit_time.
  float exit_time = 0.9f;
  bool has_exit_time = true;
  // If non-empty, transition also requires this trigger (consumed on fire).
  std::string trigger;
};

// Minimal state machine wrapping SampleClip (M26 / C10). No blend tree.
class AnimationStateMachine {
 public:
  void AddState(AnimState state);
  void AddTransition(AnimTransition transition);
  void SetState(std::string_view name);
  void SetTrigger(std::string_view name);
  void ClearTriggers();

  void Update(float dt);
  [[nodiscard]] SkinPose Sample(const Skeleton& skel) const;

  [[nodiscard]] const std::string& current_state() const { return current_; }
  [[nodiscard]] float state_time() const { return state_time_; }

 private:
  const AnimState* FindState(std::string_view name) const;
  bool TryTransition();

  std::vector<AnimState> states_;
  std::vector<AnimTransition> transitions_;
  std::unordered_map<std::string, bool> triggers_;
  std::string current_;
  float state_time_ = 0.f;
};

}  // namespace engine::animation
