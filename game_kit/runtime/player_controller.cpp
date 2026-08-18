#include "game_kit/player_controller.h"

#include "game_kit/runtime.h"

#include "engine/app/application.h"
#include "engine/input/input_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/scene/world.h"

namespace game_kit {

void PlayerController::TickMove(engine::Application& app, GameRuntime& rt, float dt) {
  if (dt <= 0.f) {
    return;
  }
  engine::Vec3 move{};
  if (app.input().key_down(engine::input::Key::W)) {
    move.z += 1.f;
  }
  if (app.input().key_down(engine::input::Key::S)) {
    move.z -= 1.f;
  }
  if (app.input().key_down(engine::input::Key::A)) {
    move.x -= 1.f;
  }
  if (app.input().key_down(engine::input::Key::D)) {
    move.x += 1.f;
  }
  if (move.length_squared() <= 0.f) {
    return;
  }
  move = engine::Normalize(move) * (move_speed * dt);

  auto* phys = rt.physics();
  if (phys && physics_body >= 0) {
    (void)phys->MoveCharacter(physics_body, move);
    return;
  }

  Entity* e = rt.entities().FindByName(entity_name);
  auto* world = rt.world() ? rt.world() : &app.world();
  if (!e || e->node == engine::scene::kInvalidNode || !world->valid(e->node)) {
    return;
  }
  auto t = world->local_transform(e->node);
  t.position = t.position + move;
  world->set_local_transform(e->node, t);
}

void PlayerController::TickView(engine::Application& app, GameRuntime& rt) {
  engine::Vec3 pos{};
  auto* world = rt.world() ? rt.world() : &app.world();
  auto* phys = rt.physics();
  if (phys && physics_body >= 0) {
    pos = phys->body_position(physics_body);
    if (Entity* e = rt.entities().FindByName(entity_name)) {
      if (e->node != engine::scene::kInvalidNode && world->valid(e->node)) {
        auto t = world->local_transform(e->node);
        t.position = pos;
        world->set_local_transform(e->node, t);
      }
    }
  } else if (Entity* e = rt.entities().FindByName(entity_name)) {
    if (e->node != engine::scene::kInvalidNode && world->valid(e->node)) {
      pos = world->local_transform(e->node).position;
    }
  }
  app.camera().position = pos + camera_offset;
  app.camera().pitch = camera_pitch;
  app.camera().yaw = 0.f;
}

}  // namespace game_kit
