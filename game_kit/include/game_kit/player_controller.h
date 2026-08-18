#pragma once

#include "engine/core/math.h"

#include <string>

namespace engine {
class Application;
}

namespace game_kit {

class GameRuntime;

// Genre-agnostic WASD + follow-camera skeleton. Physics MoveCharacter is optional.
class PlayerController {
 public:
  std::string entity_name = "player";
  float move_speed = 6.f;
  engine::Vec3 camera_offset{0.f, 3.5f, -6.f};
  float camera_pitch = -0.35f;
  int physics_body = -1;

  // Apply WASD. If physics_body >= 0 and runtime has physics, uses MoveCharacter.
  // Caller is responsible for IPhysicsWorld::Step.
  void TickMove(engine::Application& app, GameRuntime& rt, float dt);

  // Sync node from physics (if any) and update follow camera.
  void TickView(engine::Application& app, GameRuntime& rt);
};

}  // namespace game_kit
