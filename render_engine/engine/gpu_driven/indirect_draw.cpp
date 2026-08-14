#include "engine/gpu_driven/indirect_draw.h"

namespace engine::gpu_driven {

void FillIndirectArgs(IndirectDrawArgs& out, std::uint32_t index_count,
                      std::uint32_t instance_count) {
  out.index_count_per_instance = index_count;
  out.instance_count = instance_count;
  out.start_index_location = 0;
  out.base_vertex_location = 0;
  out.start_instance_location = 0;
}

}  // namespace engine::gpu_driven
