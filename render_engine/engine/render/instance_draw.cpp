#include "engine/render/instance_draw.h"

#include <cstring>

namespace engine::render {

std::vector<std::uint8_t> BuildInstanceBuffer(std::span<const InstanceData> instances) {
  std::vector<std::uint8_t> out;
  out.resize(instances.size() * sizeof(Mat4));
  for (std::size_t i = 0; i < instances.size(); ++i) {
    std::memcpy(out.data() + i * sizeof(Mat4), instances[i].world.m.data(), sizeof(Mat4));
  }
  return out;
}

}  // namespace engine::render
