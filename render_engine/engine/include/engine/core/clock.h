#pragma once

namespace engine {

class Clock {
 public:
  // Seconds since construction (steady clock).
  [[nodiscard]] double elapsed_seconds() const;

  // Seconds since previous Tick() (first call returns ~0).
  double Tick();

 private:
  double start_seconds_ = NowSeconds();
  double last_tick_seconds_ = start_seconds_;

  static double NowSeconds();
};

}  // namespace engine
