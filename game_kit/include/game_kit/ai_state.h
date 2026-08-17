#pragma once

#include <cstdint>
#include <string_view>

namespace game_kit {

// GK5 thin: simple AI state enum skeleton (no behavior tree / nav).
enum class AiState : std::uint8_t {
  Idle = 0,
  Patrol,
  Chase,
  Attack,
  Flee,
  Dead,
};

[[nodiscard]] inline std::string_view ToString(AiState s) {
  switch (s) {
    case AiState::Idle:
      return "Idle";
    case AiState::Patrol:
      return "Patrol";
    case AiState::Chase:
      return "Chase";
    case AiState::Attack:
      return "Attack";
    case AiState::Flee:
      return "Flee";
    case AiState::Dead:
      return "Dead";
  }
  return "Unknown";
}

struct AiStateMachine {
  AiState state = AiState::Idle;
  AiState previous = AiState::Idle;

  void Set(AiState next) {
    if (next == state) {
      return;
    }
    previous = state;
    state = next;
  }
};

}  // namespace game_kit
