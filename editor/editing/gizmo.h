#pragma once

#include "editing/ray.h"

#include "engine/core/math.h"
#include "engine/debug/debug_draw.h"
#include "engine/scene/world.h"

#include <span>

namespace editor {

enum class GizmoMode { Move = 0, Rotate = 1, Scale = 2 };

inline constexpr float kGizmoLength = 1.4f;
inline constexpr float kGizmoHitRadius = 0.18f;

void DrawGizmo(engine::debug::DebugDraw& draw, const engine::scene::World& world,
               engine::scene::NodeId node, GizmoMode mode = GizmoMode::Move, bool local = false);

void DrawGizmos(engine::debug::DebugDraw& draw, const engine::scene::World& world,
                std::span<const engine::scene::NodeId> nodes, GizmoMode mode = GizmoMode::Move,
                bool local = false);

void DrawBounds(engine::debug::DebugDraw& draw, const engine::scene::World& world,
                std::span<const engine::scene::NodeId> nodes);

// Apply accumulated XZ pixel drag from original transforms. Optional grid snap.
bool TranslateSelection(engine::scene::World& world, std::span<const engine::scene::NodeId> nodes,
                        std::span<const engine::scene::Transform> origins, float acc_x_px,
                        float acc_z_px, float sensitivity, bool snap, float grid);

bool TranslateSelectionDelta(engine::scene::World& world, std::span<const engine::scene::NodeId> nodes,
                             std::span<const engine::scene::Transform> origins, float dx, float dz,
                             bool snap, float grid);

// axis_delta: Move = world units along axis; Rotate = radians; Scale = additive scale.
bool ApplyGizmo(engine::scene::World& world, std::span<const engine::scene::NodeId> nodes,
                std::span<const engine::scene::Transform> origins, GizmoMode mode, Axis axis,
                float axis_delta, bool snap, float grid, bool local = false);

}  // namespace editor
