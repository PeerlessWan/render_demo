#include "editing/ops.h"

#include "engine/core/math.h"

namespace editor {

engine::scene::NodeId DuplicateNode(engine::scene::World& world, engine::scene::NodeId src,
                                    float offset) {
  if (!world.valid(src)) {
    return engine::scene::kInvalidNode;
  }
  const std::string name = world.name(src).empty() ? "copy" : world.name(src) + "_copy";
  const auto id = world.CreateNode(name);
  auto t = world.local_transform(src);
  t.position.x += offset;
  world.set_local_transform(id, t);
  if (const auto* mesh = world.mesh(src)) {
    world.set_mesh(id, *mesh);
  }
  world.set_visible(id, world.visible(src));
  return id;
}

std::vector<engine::scene::NodeId> DuplicateSelection(engine::scene::World& world, const Selection& sel,
                                                      float offset) {
  std::vector<engine::scene::NodeId> created;
  for (auto id : sel.All()) {
    const auto copy = DuplicateNode(world, id, offset);
    if (copy != engine::scene::kInvalidNode) {
      created.push_back(copy);
    }
  }
  return created;
}

void DestroySelection(engine::scene::World& world, Selection* sel) {
  if (!sel) {
    return;
  }
  for (auto id : sel->All()) {
    (void)world.DestroyNode(id);
  }
  sel->Clear();
}

void FrameCamera(engine::render::Camera* cam, const engine::scene::World& world,
                 engine::scene::NodeId node) {
  if (!cam || !world.valid(node)) {
    return;
  }
  const auto p = world.local_transform(node).position;
  cam->position = p + engine::Vec3{0.f, 2.5f, 6.f};
  cam->yaw = 0.f;
  cam->pitch = -0.25f;
}

}  // namespace editor
