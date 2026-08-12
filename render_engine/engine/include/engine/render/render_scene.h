#pragma once

#include "engine/core/math.h"
#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render {

// Immutable render snapshot — extract only; never write back to World.
struct RenderInstance {
  scene::NodeId node = scene::kInvalidNode;
  Mat4 world = Mat4::Identity();
  Aabb world_bounds{};
  std::string mesh_id;
};

struct RenderScene {
  Camera camera{};
  std::vector<RenderInstance> instances;
  std::uint32_t culled = 0;
};

class RenderSceneExtractor {
 public:
  // Copies visible, frustum-accepted mesh nodes into SoA-ish instance list.
  static RenderScene Extract(const scene::World& world, const Camera& camera, float aspect);
};

}  // namespace engine::render
