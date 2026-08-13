#include "engine/physics/i_physics_world.h"

namespace engine::physics {

std::unique_ptr<IPhysicsWorld> CreateJoltPhysicsWorld() {
  return nullptr;
}

}  // namespace engine::physics
