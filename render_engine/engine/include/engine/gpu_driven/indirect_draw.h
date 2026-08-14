#pragma once

#include <cstdint>

namespace engine::gpu_driven {

// D3D12/Vulkan-compatible indexed indirect draw args (5 x u32).
struct IndirectDrawArgs {
  std::uint32_t index_count_per_instance = 0;
  std::uint32_t instance_count = 0;
  std::uint32_t start_index_location = 0;
  std::int32_t base_vertex_location = 0;
  std::uint32_t start_instance_location = 0;
};

// Fill args for a single draw batch. Requires FeatureSet::bindless or indirect path (M24).
void FillIndirectArgs(IndirectDrawArgs& out, std::uint32_t index_count, std::uint32_t instance_count);

}  // namespace engine::gpu_driven
