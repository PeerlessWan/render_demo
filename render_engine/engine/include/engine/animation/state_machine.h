#pragma once

#include "engine/animation/skeleton.h"

#include <span>
#include <string>
#include <string_view>
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

struct BlendLayer {
  std::string state;
  float weight = 1.f;
  float time = 0.f;
};

// C10 / Mega-W8: timed notify markers on a named state (host consumes fired events).
struct AnimNotify {
  std::string name;
  float time = 0.f;  // seconds in clip local time
};

struct AnimNotifyEvent {
  std::string state;
  std::string name;
  float time = 0.f;
};

// Minimal state machine wrapping SampleClip (M26 / C10). W6 adds multi-clip blend.
// Mega-W8: AnimNotify list + DrainNotifies.
class AnimationStateMachine {
 public:
  void AddState(AnimState state);
  void AddTransition(AnimTransition transition);
  void SetState(std::string_view name);
  void SetTrigger(std::string_view name);
  void ClearTriggers();

  void AddNotify(std::string_view state, AnimNotify notify);
  [[nodiscard]] std::span<const AnimNotify> NotifiesFor(std::string_view state) const;
  // Events fired during the last Update() (cleared at the start of each Update).
  [[nodiscard]] std::span<const AnimNotifyEvent> DrainNotifies();

  void Update(float dt);
  [[nodiscard]] SkinPose Sample(const Skeleton& skel) const;

  // W6/C10: weighted blend of multiple named states (weights renormalized; missing → skip).
  [[nodiscard]] SkinPose SampleBlend(const Skeleton& skel, std::span<const BlendLayer> layers) const;

  [[nodiscard]] const std::string& current_state() const { return current_; }
  [[nodiscard]] float state_time() const { return state_time_; }

 private:
  const AnimState* FindState(std::string_view name) const;
  bool TryTransition();
  void CollectNotifiesCrossing(float prev_time, float curr_time);

  std::vector<AnimState> states_;
  std::vector<AnimTransition> transitions_;
  std::unordered_map<std::string, bool> triggers_;
  std::unordered_map<std::string, std::vector<AnimNotify>> notifies_;
  std::vector<AnimNotifyEvent> fired_;
  std::string current_;
  float state_time_ = 0.f;
};

}  // namespace engine::animation
