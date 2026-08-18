#pragma once

#include "engine/rhi/i_device.h"
#include "engine/scene/world.h"

#include <filesystem>
#include <vector>

namespace editor {

[[nodiscard]] engine::Status BakeSceneLights(const engine::scene::World& world,
                                             const std::vector<float>& heights,
                                             const std::filesystem::path& out_rgba,
                                             engine::rhi::IDevice* device);

}  // namespace editor
