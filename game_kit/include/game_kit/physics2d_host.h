#pragma once

#include "engine/physics/i_physics_world2d.h"

namespace game_kit {

// Thin host helpers so Lua/Host can call Physics2D without knowing backends (ADR 0049).
inline engine::physics::RayHit2D HostRaycast2D(engine::physics::IPhysicsWorld2D* world,
                                              const engine::Vec2& origin, const engine::Vec2& dir,
                                              float max_dist, std::uint32_t mask = 0xFFFFFFFFu) {
  if (!world) {
    return {};
  }
  return world->Raycast2D(origin, dir, max_dist, mask);
}

inline engine::Status HostMoveAndSlide(engine::physics::IPhysicsWorld2D* world, int character_id,
                                       const engine::Vec2& velocity, float dt) {
  if (!world) {
    return engine::Status::Fail("no physics2d");
  }
  return world->MoveAndSlide(character_id, velocity, dt);
}

}  // namespace game_kit
