#include "sim/time.h"

#include <cmath>

namespace mc {

void Clock::Tick(float dt) {
  time += dt * 20.f;
  while (time >= 24000.f) {
    time -= 24000.f;
  }
  while (time < 0.f) {
    time += 24000.f;
  }
}

bool Clock::Night() const { return time > 13000.f || time < 2000.f; }

void Clock::SleepToDawn() { time = 6000.f; }

void Clock::Apply(engine::render::Environment* env) const {
  if (!env) {
    return;
  }
  const float t = time / 24000.f;
  const float ang = t * 6.2831853f;
  // Keep a downward sun so untextured cubes stay readable on the LDR backbuffer.
  float sy = -std::cos(ang);
  if (sy > -0.25f && !Night()) {
    sy = -0.25f;
  }
  env->sun_direction = {std::sin(ang) * 0.55f, sy, 0.25f};
  const float day = Night() ? 0.2f : 1.f;
  env->sun_intensity = 2.4f * day;
  env->ambient = Night() ? engine::ColorRgba{0.10f, 0.11f, 0.14f, 1.f}
                         : engine::ColorRgba{0.48f, 0.50f, 0.46f, 1.f};
  env->clear_color = Night() ? engine::ColorRgba{0.02f, 0.03f, 0.06f, 1.f}
                             : engine::ColorRgba{0.42f, 0.62f, 0.88f, 1.f};
}

}  // namespace mc
