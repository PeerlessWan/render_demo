#pragma once

#include "engine/core/math.h"
#include "engine/debug/debug_draw.h"
#include "engine/scene/world.h"

namespace editor {

struct Selection;

void DrawGizmo(engine::debug::DebugDraw& draw, const engine::scene::World& world,
               engine::scene::NodeId node);

// Translate on XZ from mouse pixel delta. Returns new transform if changed.
bool TranslateXz(engine::scene::World& world, engine::scene::NodeId node, float dx_px, float dy_px,
                 float sensitivity, engine::scene::Transform* out_new);

}  // namespace editor
