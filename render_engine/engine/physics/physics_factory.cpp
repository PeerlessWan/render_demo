#include "engine/physics/i_physics_world.h"

namespace engine::physics {

std::unique_ptr<IPhysicsWorld> CreateDefaultPhysicsWorld() {
#if defined(ENGINE_WITH_JOLT) && ENGINE_WITH_JOLT
  if (auto jolt = CreateJoltPhysicsWorld()) {
    return jolt;
  }
#endif
  return CreateBuiltinPhysicsWorld();
}

}  // namespace engine::physics
