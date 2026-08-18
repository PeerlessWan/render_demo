#pragma once

#include "engine/animation/blend_tree.h"
#include "engine/animation/state_machine.h"
#include "engine/core/result.h"

#include <string>
#include <string_view>

namespace engine::animation {

// Mega-W9: minimal JSON-ish / line text for BlendTree root or StateMachine topology.
// Round-trips op/params/state names/transitions/crossfade; clip tracks are name+duration only.

[[nodiscard]] std::string SerializeBlendTree(const BlendTreeNode& root);
[[nodiscard]] Status DeserializeBlendTree(std::string_view text, BlendTreeNode& out);

[[nodiscard]] std::string SerializeStateMachine(const AnimationStateMachine& sm);
[[nodiscard]] Status DeserializeStateMachine(std::string_view text, AnimationStateMachine& out);

}  // namespace engine::animation
