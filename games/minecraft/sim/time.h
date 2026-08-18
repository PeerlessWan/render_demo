#pragma once

#include "engine/render/environment.h"

namespace mc {

struct Clock {
  float time = 6000.f;
  void Tick(float dt);
  [[nodiscard]] bool Night() const;
  void SleepToDawn();
  void Apply(engine::render::Environment* env) const;
};

}  // namespace mc
