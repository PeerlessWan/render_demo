#include "gizmo.h"

#include "editing/snap.h"

#include <algorithm>
#include <cmath>

namespace editor {
namespace {

engine::Quat MulQuat(const engine::Quat& a, const engine::Quat& b) {
  engine::Quat q;
  q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
  q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
  q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
  q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
  return q;
}

engine::Quat AxisDelta(Axis axis, float radians) {
  switch (axis) {
    case Axis::X:
      return engine::Quat::FromEulerYxz(0.f, radians, 0.f);
    case Axis::Y:
      return engine::Quat::FromEulerYxz(radians, 0.f, 0.f);
    case Axis::Z:
      return engine::Quat::FromEulerYxz(0.f, 0.f, radians);
    default:
      return engine::Quat::Identity();
  }
}

void AddThickLine(engine::debug::DebugDraw& draw, const engine::Vec3& a, const engine::Vec3& b,
                  const engine::Vec3& off, const engine::ColorRgba& color) {
  draw.AddLine(a, b, color);
  draw.AddLine(a + off, b + off, color);
  draw.AddLine(a + off * -1.f, b + off * -1.f, color);
}

void AddRing(engine::debug::DebugDraw& draw, const engine::Vec3& center, const engine::Vec3& axis,
             float radius, const engine::ColorRgba& color) {
  const engine::Vec3 n = engine::Normalize(axis);
  engine::Vec3 t = std::fabs(n.y) < 0.9f ? engine::Cross(n, {0.f, 1.f, 0.f})
                                         : engine::Cross(n, {1.f, 0.f, 0.f});
  t = engine::Normalize(t);
  const engine::Vec3 b = engine::Cross(n, t);
  constexpr int kSeg = 28;
  engine::Vec3 prev = center + t * radius;
  for (int i = 1; i <= kSeg; ++i) {
    const float ang = static_cast<float>(i) / static_cast<float>(kSeg) * 6.2831853f;
    const engine::Vec3 p = center + t * (std::cos(ang) * radius) + b * (std::sin(ang) * radius);
    draw.AddLine(prev, p, color);
    prev = p;
  }
}

}  // namespace

void DrawGizmo(engine::debug::DebugDraw& draw, const engine::scene::World& world,
               engine::scene::NodeId node, GizmoMode mode, bool local, float scale) {
  if (!world.valid(node)) {
    return;
  }
  const float s = std::clamp(scale, 0.35f, 12.f);
  const float len = kGizmoLength * s;
  const auto& t = world.local_transform(node);
  engine::Aabb box;
  box.min = t.position + engine::Vec3{-0.5f, 0.f, -0.5f};
  box.max = t.position + engine::Vec3{0.5f, 1.f, 0.5f};
  draw.AddAabb(box, {1.f, 0.85f, 0.2f, 1.f});
  const auto p = t.position;
  const engine::Vec3 x = GizmoAxisDir(Axis::X, t.rotation, local) * len;
  const engine::Vec3 y = GizmoAxisDir(Axis::Y, t.rotation, local) * len;
  const engine::Vec3 z = GizmoAxisDir(Axis::Z, t.rotation, local) * len;
  const float thick = 0.03f * s;
  AddThickLine(draw, p, p + x, {0.f, thick, 0.f}, {1.f, 0.2f, 0.2f, 1.f});
  AddThickLine(draw, p, p + y, {thick, 0.f, 0.f}, {0.2f, 1.f, 0.2f, 1.f});
  AddThickLine(draw, p, p + z, {thick, 0.f, 0.f}, {0.3f, 0.5f, 1.f, 1.f});
  if (mode == GizmoMode::Rotate) {
    AddRing(draw, p, GizmoAxisDir(Axis::X, t.rotation, local), kGizmoRingRadius * s,
            {1.f, 0.35f, 0.35f, 1.f});
    AddRing(draw, p, GizmoAxisDir(Axis::Y, t.rotation, local), kGizmoRingRadius * s,
            {0.35f, 1.f, 0.35f, 1.f});
    AddRing(draw, p, GizmoAxisDir(Axis::Z, t.rotation, local), kGizmoRingRadius * s,
            {0.4f, 0.55f, 1.f, 1.f});
  }
}

void DrawGizmos(engine::debug::DebugDraw& draw, const engine::scene::World& world,
                std::span<const engine::scene::NodeId> nodes, GizmoMode mode, bool local, float scale) {
  if (nodes.empty()) {
    return;
  }
  DrawGizmo(draw, world, nodes.front(), mode, local, scale);
  for (std::size_t i = 1; i < nodes.size(); ++i) {
    if (!world.valid(nodes[i])) {
      continue;
    }
    const auto& t = world.local_transform(nodes[i]);
    engine::Aabb box;
    box.min = t.position + engine::Vec3{-0.4f, 0.f, -0.4f};
    box.max = t.position + engine::Vec3{0.4f, 0.8f, 0.4f};
    draw.AddAabb(box, {1.f, 0.85f, 0.2f, 0.6f});
  }
}

float GizmoWorldScale(const engine::Vec3& camera, const engine::Vec3& pivot) {
  const float d = (camera - pivot).length();
  return std::clamp(d * 0.12f, 0.6f, 8.f);
}

void DrawBounds(engine::debug::DebugDraw& draw, const engine::scene::World& world,
                std::span<const engine::scene::NodeId> nodes) {
  for (auto id : nodes) {
    if (!world.valid(id)) {
      continue;
    }
    const auto* mesh = world.mesh(id);
    engine::Aabb local = mesh ? mesh->local_bounds : engine::Aabb{engine::Vec3{-0.5f, -0.5f, -0.5f},
                                                                  engine::Vec3{0.5f, 0.5f, 0.5f}};
    const auto& m = world.world_matrix(id);
    engine::Vec3 corners[8] = {
        {local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
        {local.min.x, local.max.y, local.min.z}, {local.max.x, local.max.y, local.min.z},
        {local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
        {local.min.x, local.max.y, local.max.z}, {local.max.x, local.max.y, local.max.z},
    };
    engine::Aabb box;
    box.min = {1e9f, 1e9f, 1e9f};
    box.max = {-1e9f, -1e9f, -1e9f};
    for (auto c : corners) {
      const auto w = m.TransformPoint(c);
      box.min.x = std::min(box.min.x, w.x);
      box.min.y = std::min(box.min.y, w.y);
      box.min.z = std::min(box.min.z, w.z);
      box.max.x = std::max(box.max.x, w.x);
      box.max.y = std::max(box.max.y, w.y);
      box.max.z = std::max(box.max.z, w.z);
    }
    draw.AddAabb(box, {0.3f, 0.9f, 1.f, 1.f});
  }
}

bool TranslateSelectionDelta(engine::scene::World& world, std::span<const engine::scene::NodeId> nodes,
                             std::span<const engine::scene::Transform> origins, float dx, float dz,
                             bool snap, float grid) {
  if (nodes.size() != origins.size() || nodes.empty()) {
    return false;
  }
  bool any = false;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (!world.valid(nodes[i])) {
      continue;
    }
    auto t = origins[i];
    t.position.x += dx;
    t.position.z += dz;
    if (snap) {
      SnapTransform(&t, grid);
    }
    world.set_local_transform(nodes[i], t);
    any = true;
  }
  return any;
}

bool TranslateSelection(engine::scene::World& world, std::span<const engine::scene::NodeId> nodes,
                        std::span<const engine::scene::Transform> origins, float acc_x_px,
                        float acc_z_px, float sensitivity, bool snap, float grid) {
  return TranslateSelectionDelta(world, nodes, origins, acc_x_px * sensitivity, -acc_z_px * sensitivity,
                                 snap, grid);
}

bool ApplyGizmo(engine::scene::World& world, std::span<const engine::scene::NodeId> nodes,
                std::span<const engine::scene::Transform> origins, GizmoMode mode, Axis axis,
                float axis_delta, bool snap, float grid, bool local) {
  if (nodes.size() != origins.size() || nodes.empty() || axis == Axis::None) {
    return false;
  }
  bool any = false;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (!world.valid(nodes[i])) {
      continue;
    }
    auto t = origins[i];
    const engine::Vec3 dir = GizmoAxisDir(axis, origins[i].rotation, local);
    if (mode == GizmoMode::Move) {
      t.position = origins[i].position + dir * axis_delta;
      if (snap) {
        SnapTransform(&t, grid);
      }
    } else if (mode == GizmoMode::Rotate) {
      if (local) {
        t.rotation = MulQuat(origins[i].rotation, AxisDelta(axis, axis_delta));
      } else {
        t.rotation = MulQuat(AxisDelta(axis, axis_delta), origins[i].rotation);
      }
    } else {
      float sx = origins[i].scale.x;
      float sy = origins[i].scale.y;
      float sz = origins[i].scale.z;
      if (axis == Axis::X) {
        sx += axis_delta;
      } else if (axis == Axis::Y) {
        sy += axis_delta;
      } else {
        sz += axis_delta;
      }
      const auto clamp_s = [](float v) {
        if (v < 0.1f) {
          return 0.1f;
        }
        if (v > 8.f) {
          return 8.f;
        }
        return v;
      };
      t.scale.x = clamp_s(sx);
      t.scale.y = clamp_s(sy);
      t.scale.z = clamp_s(sz);
      if (snap) {
        t.scale = SnapVec3(t.scale, grid);
        t.scale.x = clamp_s(t.scale.x);
        t.scale.y = clamp_s(t.scale.y);
        t.scale.z = clamp_s(t.scale.z);
      }
    }
    world.set_local_transform(nodes[i], t);
    any = true;
  }
  return any;
}

}  // namespace editor
