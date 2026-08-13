#include "engine/physics/i_physics_world.h"

namespace engine::physics {

// M12: real Jolt backend not linked yet. Keep factory for ENGINE_WITH_JOLT wiring.
std::unique_ptr<IPhysicsWorld> CreateJoltPhysicsWorld() {
#if defined(ENGINE_WITH_JOLT) && ENGINE_WITH_JOLT
  // When Jolt is vendored/linked, replace this stub with a real IPhysicsWorld.
  return nullptr;
#else
  return nullptr;
#endif
}

std::unique_ptr<IPhysicsWorld> CreateDefaultPhysicsWorld() {
#if defined(ENGINE_WITH_JOLT) && ENGINE_WITH_JOLT
  if (auto jolt = CreateJoltPhysicsWorld()) {
    return jolt;
  }
#endif
  return CreateBuiltinPhysicsWorld();
}

}  // namespace engine::physics
