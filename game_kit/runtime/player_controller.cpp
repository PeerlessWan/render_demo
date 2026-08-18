#include "game_kit/player_controller.h"

#include "game_kit/runtime.h"

#include "engine/app/application.h"
#include "engine/core/math.h"
#include "engine/input/input_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/scene/world.h"

#include <cmath>

namespace game_kit {

void PlayerController::InstallPlayDefaults(engine::input::InputSystem& input) {
  input.InstallFlyCameraDefaults();
}

void PlayerController::TickMove(engine::Application& app, GameRuntime& rt, float dt) {
  TickMove(app.input(), rt, dt);
}

void PlayerController::TickMove(engine::input::InputSystem& input, GameRuntime& rt, float dt) {
  if (dt <= 0.f) {
    return;
  }
  input.EvaluateActions();

  if (use_actions) {
    camera_yaw += input.axis("LookX") * look_sensitivity;
    camera_pitch -= input.axis("LookY") * look_sensitivity;
    if (camera_pitch > 1.5f) {
      camera_pitch = 1.5f;
    }
    if (camera_pitch < -1.5f) {
      camera_pitch = -1.5f;
    }
  }

  engine::Vec3 move{};
  if (use_actions) {
    move.x = input.axis("MoveX");
    move.z = input.axis("MoveZ");
  } else {
    if (input.key_down(engine::input::Key::W)) {
      move.z += 1.f;
    }
    if (input.key_down(engine::input::Key::S)) {
      move.z -= 1.f;
    }
    if (input.key_down(engine::input::Key::A)) {
      move.x -= 1.f;
    }
    if (input.key_down(engine::input::Key::D)) {
      move.x += 1.f;
    }
  }
  float speed = move_speed;
  if (use_actions && input.axis("MoveY") > 0.5f) {
    speed *= sprint_multiplier;
  }
  if (move.length_squared() > 0.f) {
    move = engine::Normalize(move);
    if (camera_relative_move) {
      const float cy = std::cos(camera_yaw);
      const float sy = std::sin(camera_yaw);
      const engine::Vec3 world{move.x * cy + move.z * sy, 0.f, -move.x * sy + move.z * cy};
      move = world * (speed * dt);
    } else {
      move = move * (speed * dt);
    }
  } else {
    move = {};
  }

  const bool jump_down = use_actions ? input.pressed("Jump") : input.key_down(engine::input::Key::Space);
  const bool jump = jump_down && !jump_held_ && grounded;
  jump_held_ = jump_down;
  if (jump) {
    vertical_vel = jump_speed;
    grounded = false;
  }
  if (!grounded) {
    vertical_vel -= gravity * dt;
    move.y += vertical_vel * dt;
  }

  auto* phys = rt.physics();
  if (phys && physics_body >= 0) {
    (void)phys->MoveCharacter(physics_body, move);
    const auto pos = phys->body_position(physics_body);
    const auto he = phys->body_half_extents(physics_body);
    const auto hit = phys->Raycast(pos + engine::Vec3{0.f, 0.05f, 0.f}, {0.f, -1.f, 0.f},
                                   he.y * 2.f + 0.6f);
    grounded = hit.hit && hit.distance <= he.y + 0.35f;
    if (grounded) {
      vertical_vel = 0.f;
    }
    Entity* e = rt.entities().FindByName(entity_name);
    auto* world = rt.world();
    if (e && world && e->node != engine::scene::kInvalidNode && world->valid(e->node)) {
      auto t = world->local_transform(e->node);
      t.position = pos;
      world->set_local_transform(e->node, t);
    }
    return;
  }

  Entity* e = rt.entities().FindByName(entity_name);
  auto* world = rt.world();
  if (!e || !world || e->node == engine::scene::kInvalidNode || !world->valid(e->node)) {
    return;
  }
  auto t = world->local_transform(e->node);
  t.position = t.position + move;
  if (t.position.y <= ground_y) {
    t.position.y = ground_y;
    vertical_vel = 0.f;
    grounded = true;
  }
  world->set_local_transform(e->node, t);
}

void PlayerController::TickView(engine::Application& app, GameRuntime& rt) {
  TickView(app.camera(), rt);
}

void PlayerController::TickView(engine::render::Camera& camera, GameRuntime& rt) {
  engine::Vec3 pos{};
  auto* world = rt.world();
  auto* phys = rt.physics();
  if (phys && physics_body >= 0) {
    pos = phys->body_position(physics_body);
    if (Entity* e = rt.entities().FindByName(entity_name)) {
      if (world && e->node != engine::scene::kInvalidNode && world->valid(e->node)) {
        auto t = world->local_transform(e->node);
        t.position = pos;
        world->set_local_transform(e->node, t);
      }
    }
  } else if (Entity* e = rt.entities().FindByName(entity_name)) {
    if (world && e->node != engine::scene::kInvalidNode && world->valid(e->node)) {
      pos = world->local_transform(e->node).position;
    }
  }

  if (camera_mode == CameraMode::FirstPerson) {
    camera.position = pos + engine::Vec3{0.f, arm_height, 0.f};
    camera.pitch = camera_pitch;
    camera.yaw = camera_yaw;
    return;
  }
  if (camera_mode == CameraMode::Follow) {
    camera.position = pos + camera_offset;
    camera.pitch = camera_pitch;
    camera.yaw = camera_yaw;
    return;
  }

  const float cp = std::cos(camera_pitch);
  const float sp = std::sin(camera_pitch);
  const float cy = std::cos(camera_yaw);
  const float sy = std::sin(camera_yaw);
  engine::Vec3 back{sy * cp, -sp, cy * cp};
  camera.position = pos + engine::Vec3{0.f, arm_height, 0.f} + back * arm_distance;
  camera.pitch = camera_pitch;
  camera.yaw = camera_yaw;
}

}  // namespace game_kit
