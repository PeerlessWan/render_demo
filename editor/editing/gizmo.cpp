#include "gizmo.h"

namespace editor {

void DrawGizmo(engine::debug::DebugDraw& draw, const engine::scene::World& world,
               engine::scene::NodeId node) {
  if (!world.valid(node)) {
    return;
  }
  const auto& t = world.local_transform(node);
  engine::Aabb box;
  box.min = t.position + engine::Vec3{-0.5f, 0.f, -0.5f};
  box.max = t.position + engine::Vec3{0.5f, 1.f, 0.5f};
  draw.AddAabb(box, {1.f, 0.85f, 0.2f, 1.f});
  const auto p = t.position;
  draw.AddLine(p, p + engine::Vec3{1.2f, 0.f, 0.f}, {1.f, 0.2f, 0.2f, 1.f});
  draw.AddLine(p, p + engine::Vec3{0.f, 1.2f, 0.f}, {0.2f, 1.f, 0.2f, 1.f});
  draw.AddLine(p, p + engine::Vec3{0.f, 0.f, 1.2f}, {0.3f, 0.5f, 1.f, 1.f});
}

bool TranslateXz(engine::scene::World& world, engine::scene::NodeId node, float dx_px, float dy_px,
                 float sensitivity, engine::scene::Transform* out_new) {
  if (!world.valid(node) || !out_new) {
    return false;
  }
  auto t = world.local_transform(node);
  t.position.x += dx_px * sensitivity;
  t.position.z -= dy_px * sensitivity;
  world.set_local_transform(node, t);
  *out_new = t;
  return true;
}

}  // namespace editor
