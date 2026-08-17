#pragma once

#include "engine/core/math.h"
#include "engine/render/occlusion.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::gpu_driven {

// D3D12/Vulkan-compatible indexed indirect draw args (5 x u32).
struct IndirectDrawArgs {
  std::uint32_t index_count_per_instance = 0;
  std::uint32_t instance_count = 0;
  std::uint32_t start_index_location = 0;
  std::int32_t base_vertex_location = 0;
  std::uint32_t start_instance_location = 0;
};

void FillIndirectArgs(IndirectDrawArgs& out, std::uint32_t index_count, std::uint32_t instance_count);

// CPU cull (frustum + optional HiZ) → compact visible worlds + single IndirectDrawArgs batch.
// Acts as the "CS write IndirectArgs" stand-in until a GPU cull CS ships; same contract.
// M26/C08: reserves capacity and front-to-back sorts survivors for early-Z.
// W6: meshlet path remains Feature-gated (see MeshletPathAvailable); cull stays Indirect.
std::uint32_t CullInstancesToIndirect(std::span<const Mat4> worlds, std::span<const Aabb> local_bounds,
                                      const Mat4& view_proj, const render::OcclusionBuffer* occ,
                                      std::vector<Mat4>& out_visible, IndirectDrawArgs& out_args,
                                      std::uint32_t index_count_per_instance);

// W6/C08: Meshlet / Mesh Shader path. Default false; override Feature "meshlet" for experiments.
[[nodiscard]] bool MeshletPathAvailable();

// Pack IndirectDrawArgs as 5×u32 for UploadIndirectIndexedArgs.
std::vector<std::uint32_t> PackIndirectArgsU32(const IndirectDrawArgs& args);

}  // namespace engine::gpu_driven
