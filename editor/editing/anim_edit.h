#pragma once

#include "engine/animation/state_machine.h"

#include <string>
#include <utility>
#include <vector>

namespace editor {

struct AnimGraphEdit {
  std::vector<std::string> states{"idle", "walk", "run"};
  std::vector<std::pair<std::string, std::string>> transitions{{"idle", "walk"}, {"walk", "run"}};
  int current = 0;
  // Simple curve: 4 keys in [0,1] sampled as a weight envelope.
  float keys[4]{0.f, 0.35f, 0.8f, 1.f};
};

[[nodiscard]] std::string CurrentState(const AnimGraphEdit& g);

void AddState(AnimGraphEdit* g, std::string name);
void RemoveState(AnimGraphEdit* g, int index);
void AddTransition(AnimGraphEdit* g, std::string from, std::string to);

engine::animation::AnimationStateMachine BuildMachine(const AnimGraphEdit& g);

[[nodiscard]] float SampleCurve(const AnimGraphEdit& g, float t);

}  // namespace editor
