#pragma once

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include <filesystem>
#include <string>

namespace engine::scene {

Status SaveWorldJson(const World& world, const std::filesystem::path& path);
Result<World> LoadWorldJson(const std::filesystem::path& path);

}  // namespace engine::scene
