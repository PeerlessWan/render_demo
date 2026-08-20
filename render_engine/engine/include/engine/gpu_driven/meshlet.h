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

// Prefer meshoptimizer when third_party/meshoptimizer is present; otherwise AABB fallback.
[[nodiscard]] MeshletCookResult MeshletizePreferMeshoptimizer(std::span<const Vec3> positions,
                                                              std::span<const std::uint32_t> indices,
                                                              int grid_div);

// CPU cull → compact visible meshlet ids + one IndirectDrawArgs per survivor.
// Stand-in for a future mesh-shader / amplification cull CS (C08).
std::uint32_t CullMeshletsToIndirect(std::span<const Meshlet> meshlets, const Mat4& world,
                                     const Mat4& view_proj, const render::OcclusionBuffer* occ,
                                     std::vector<std::uint32_t>& out_visible_ids,
                                     std::vector<IndirectDrawArgs>& out_args);

// Mega-W9 C08: Feature "meshlet"/"mesh_shader" off → Unavailable SKIP.
// D3D12: probe MeshShaderTier + attempt real MS PSO (meshlet_ms.cso); optional DispatchMesh.
// Mega-W11: if D3D12 path fails, VK_EXT_mesh_shader probe → minimal Ok; else SKIP.
// W20: prefer IDevice::TryMeshShaderHotPath on the live device when available; this
// standalone probe remains for unit tests / Feature gating without an IDevice.
[[nodiscard]] Status TryMeshShaderPath();

// Alias kept for W8 call sites / tests.
[[nodiscard]] inline Status TryMeshShaderPathStub() { return TryMeshShaderPath(); }

// VK_EXT_mesh_shader device-extension probe (no draw). Ok = Supported; else Unavailable.
[[nodiscard]] Status ProbeMeshShaderSupportVk();

}  // namespace engine::gpu_driven
