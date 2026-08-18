#include "editing/anim_edit.h"

#include "engine/animation/skeleton.h"

#include <algorithm>
#include <cmath>

namespace editor {

std::string CurrentState(const AnimGraphEdit& g) {
  if (g.states.empty()) {
    return {};
  }
  const int i = std::clamp(g.current, 0, static_cast<int>(g.states.size()) - 1);
  return g.states[static_cast<std::size_t>(i)];
}

void AddState(AnimGraphEdit* g, std::string name) {
  if (!g || name.empty()) {
    return;
  }
  for (const auto& s : g->states) {
    if (s == name) {
      return;
    }
  }
  g->states.push_back(std::move(name));
}

void RemoveState(AnimGraphEdit* g, int index) {
  if (!g || index < 0 || index >= static_cast<int>(g->states.size()) || g->states.size() <= 1) {
    return;
  }
  const std::string gone = g->states[static_cast<std::size_t>(index)];
  g->states.erase(g->states.begin() + index);
  g->transitions.erase(std::remove_if(g->transitions.begin(), g->transitions.end(),
                                      [&](const auto& t) { return t.first == gone || t.second == gone; }),
                       g->transitions.end());
  g->current = std::clamp(g->current, 0, static_cast<int>(g->states.size()) - 1);
}

void AddTransition(AnimGraphEdit* g, std::string from, std::string to) {
  if (!g) {
    return;
  }
  g->transitions.emplace_back(std::move(from), std::move(to));
}

engine::animation::AnimationStateMachine BuildMachine(const AnimGraphEdit& g) {
  engine::animation::AnimationStateMachine sm;
  for (const auto& name : g.states) {
    engine::animation::AnimState st;
    st.name = name;
    st.loop = true;
    st.clip.name = name;
    st.clip.duration = 1.f;
    sm.AddState(std::move(st));
  }
  for (const auto& tr : g.transitions) {
    engine::animation::AnimTransition t;
    t.from = tr.first;
    t.to = tr.second;
    t.exit_time = 0.8f;
    sm.AddTransition(std::move(t));
  }
  if (!g.states.empty()) {
    sm.SetState(CurrentState(g));
  }
  return sm;
}

float SampleCurve(const AnimGraphEdit& g, float t) {
  t = std::clamp(t, 0.f, 1.f);
  const float x = t * 3.f;
  const int i = std::clamp(static_cast<int>(x), 0, 2);
  const float a = g.keys[i];
  const float b = g.keys[i + 1];
  const float u = x - static_cast<float>(i);
  return a + (b - a) * u;
}

}  // namespace editor
