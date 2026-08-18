#pragma once

#include "editing/selection.h"
#include "editing/undo.h"

#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include <unordered_map>
#include <vector>

namespace editor {

struct NodeMeta;

[[nodiscard]] engine::scene::NodeId FindParent(const engine::scene::World& world,
                                               engine::scene::NodeId id);

void CaptureNode(const engine::scene::World& world, engine::scene::NodeId id,
                 const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta, NodeSnap* out);

void CaptureSubtree(const engine::scene::World& world, engine::scene::NodeId root,
                    const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta,
                    std::vector<NodeSnap>* out);

void RestoreSnaps(engine::scene::World& world, std::vector<NodeSnap>* snaps,
                  std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

void ApplyProp(engine::scene::World& world, const PropSnap& p,
               std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

PropSnap CaptureProp(const engine::scene::World& world, engine::scene::NodeId id,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta);

engine::scene::NodeId DuplicateNode(engine::scene::World& world, engine::scene::NodeId src,
                                    float offset);

std::vector<engine::scene::NodeId> DuplicateSelection(engine::scene::World& world, const Selection& sel,
                                                      float offset);

void DestroySelection(engine::scene::World& world, Selection* sel);

void FrameCamera(engine::render::Camera* cam, const engine::scene::World& world,
                 engine::scene::NodeId node);

}  // namespace editor
