#pragma once

#include "engine/core/math.h"
#include "engine/render/camera.h"

#include <cstdint>
#include <string>

namespace engine {
class Application;
namespace input {
class InputSystem;
}
}

namespace game_kit {

class GameRuntime;

enum class CameraMode : std::uint8_t { Follow = 0, SpringArm = 1, FirstPerson = 2 };

// WASD/actions + jump + follow / spring-arm / first-person camera.
class PlayerController {
 public:
  std::string entity_name = "player";
  float move_speed = 6.f;
  engine::Vec3 camera_offset{0.f, 3.5f, -6.f};
  float camera_pitch = -0.35f;
  float camera_yaw = 0.f;
  float arm_distance = 6.f;
  float arm_height = 1.6f;
  float jump_speed = 6.f;
  float gravity = 18.f;
  float ground_y = 0.f;
  float look_sensitivity = 0.05f;
  float sprint_multiplier = 1.8f;
  int physics_body = -1;
  bool use_actions = true;
  bool camera_relative_move = true;
  CameraMode camera_mode = CameraMode::SpringArm;
  bool grounded = true;
  float vertical_vel = 0.f;

  static void InstallPlayDefaults(engine::input::InputSystem& input);

  void TickMove(engine::Application& app, GameRuntime& rt, float dt);
  void TickMove(engine::input::InputSystem& input, GameRuntime& rt, float dt);
  void TickView(engine::Application& app, GameRuntime& rt);
  void TickView(engine::render::Camera& camera, GameRuntime& rt);

 private:
  bool jump_held_ = false;
};

}  // namespace game_kit
