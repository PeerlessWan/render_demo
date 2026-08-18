#pragma once

#include "game_kit/scene_document.h"
#include "play/scene_play.h"

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <filesystem>
#include <unordered_map>

namespace editor {

engine::Status SaveScene(const engine::scene::World& world, const std::filesystem::path& path);
engine::Status SaveScene(const engine::scene::World& world, const std::filesystem::path& path,
                         const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta);
engine::Status LoadScene(engine::scene::World& world, const std::filesystem::path& path);

}  // namespace editor
