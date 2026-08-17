#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/render/occlusion.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::gpu_driven {

// C08: one meshlet = contiguous index range + local AABB for CPU/GPU cull.
struct Meshlet {
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
  std::uint32_t first_vertex = 0;
  std::uint32_t vertex_count = 0;
  Aabb aabb{};
};

struct MeshletCookResult {
  std::vector<Meshlet> meshlets;
  // Remapped triangle indices (3 * triangle_count); empty if input empty.
  std::vector<std::uint32_t> indices;
};

// Simple cook: bin triangles into an AABB grid (axis-aligned cells). No meshoptimizer.
// grid_div <= 0 → single meshlet covering the whole mesh.
[[nodiscard]] MeshletCookResult MeshletizeAabbGrid(std::span<const Vec3> positions,
                                                   std::span<const std::uint32_t> indices,
                                                   int grid_div);

// CPU cull → compact visible meshlet ids + one IndirectDrawArgs per survivor.
// Stand-in for a future mesh-shader / amplification cull CS (C08).
std::uint32_t CullMeshletsToIndirect(std::span<const Meshlet> meshlets, const Mat4& world,
                                     const Mat4& view_proj, const render::OcclusionBuffer* occ,
                                     std::vector<std::uint32_t>& out_visible_ids,
                                     std::vector<IndirectDrawArgs>& out_args);

// Optional D3D12 mesh-shader stub. Without Feature "meshlet" → Unavailable SKIP.
// Even when Feature is on, this wave only reports readiness (no real MS PSO).
[[nodiscard]] Status TryMeshShaderPathStub();

}  // namespace engine::gpu_driven
