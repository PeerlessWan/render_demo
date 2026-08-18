#pragma once

#include "engine/animation/state_machine.h"
#include "engine/core/math.h"
#include "engine/scene/world.h"

#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

class EventBus;
class EntityWorld;
class ScriptComponentWorld;

class AnimPlayer {
 public:
  void Play(std::string_view state, bool loop = true);
  void Stop();
  void SetTrigger(std::string_view name);
  void Update(float dt);
  void AddNotify(std::string_view state, std::string name, float time);
  void set_apply_root_motion(bool v) { apply_root_motion_ = v; }
  [[nodiscard]] bool apply_root_motion() const { return apply_root_motion_; }

  [[nodiscard]] const std::string& current_state() const { return sm_.current_state(); }
  [[nodiscard]] engine::Vec3 root_motion_delta() const { return root_delta_; }
  [[nodiscard]] engine::animation::AnimationStateMachine& machine() { return sm_; }

 private:
  void EnsureState(std::string_view name, bool loop);
  engine::animation::AnimationStateMachine sm_;
  engine::Vec3 prev_root_{};
  engine::Vec3 root_delta_{};
  bool playing_ = false;
  bool apply_root_motion_ = true;
};

class AnimPlayerWorld {
 public:
  AnimPlayer& GetOrCreate(std::string entity_name);
  void Update(float dt, EventBus& events, EntityWorld& entities, ScriptComponentWorld& scripts,
              engine::scene::World* world);
  void Clear();

 private:
  std::vector<std::pair<std::string, AnimPlayer>> players_;
};

}  // namespace game_kit
