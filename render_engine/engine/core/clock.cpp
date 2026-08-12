#include "engine/core/clock.h"

#include <chrono>

namespace engine {

double Clock::NowSeconds() {
  using clock = std::chrono::steady_clock;
  const auto now = clock::now().time_since_epoch();
  return std::chrono::duration<double>(now).count();
}

double Clock::elapsed_seconds() const { return NowSeconds() - start_seconds_; }

double Clock::Tick() {
  const double now = NowSeconds();
  const double dt = now - last_tick_seconds_;
  last_tick_seconds_ = now;
  return dt;
}

}  // namespace engine
