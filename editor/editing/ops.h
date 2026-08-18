#pragma once

#include "editing/selection.h"

#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include <vector>

namespace editor {

engine::scene::NodeId DuplicateNode(engine::scene::World& world, engine::scene::NodeId src,
                                    float offset);

std::vector<engine::scene::NodeId> DuplicateSelection(engine::scene::World& world, const Selection& sel,
                                                      float offset);

void DestroySelection(engine::scene::World& world, Selection* sel);

void FrameCamera(engine::render::Camera* cam, const engine::scene::World& world,
                 engine::scene::NodeId node);

}  // namespace editor
