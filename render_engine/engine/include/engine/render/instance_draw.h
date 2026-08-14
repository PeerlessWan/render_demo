#pragma once

#include "engine/core/math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::render {

struct InstanceData {
  Mat4 world{};
};

// CPU-side pack of instance transforms for upload (64 bytes per Mat4).
std::vector<std::uint8_t> BuildInstanceBuffer(std::span<const InstanceData> instances);

}  // namespace engine::render
