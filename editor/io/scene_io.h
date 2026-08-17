#pragma once

#include "game_kit/scene_document.h"

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <filesystem>

namespace editor {

engine::Status SaveScene(const engine::scene::World& world, const std::filesystem::path& path);
engine::Status LoadScene(engine::scene::World& world, const std::filesystem::path& path);

}  // namespace editor
