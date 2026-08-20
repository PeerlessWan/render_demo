#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/gpu_driven/meshlet.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::gpu_driven {

// W23 / ADR 0046: Nanite-like VirtualGeometry (NOT UE Nanite).
// Hierarchical cluster DAG + screen-error LOD + residency + GPU-cull stand-in.

struct ClusterNode {
  std::int32_t parent = -1;
  std::int32_t child_begin = -1;
  std::int32_t child_count = 0;
  std::uint32_t meshlet_index = 0;  // leaf → meshlet id; interior may duplicate coarse
  float error = 0.f;               // world-space geometric error (teaching metric)
  Aabb aabb{};
  bool is_leaf = true;
};

struct VirtualGeometryAsset {
  MeshletCookResult cook;
  std::vector<ClusterNode> nodes;  // DAG; roots have parent=-1
  std::uint32_t root = 0;
};

struct VirtualGeometryResidency {
  std::uint32_t page_budget = 64;
  std::vector<std::uint32_t> resident_meshlet_ids;
  std::vector<std::uint32_t> request_queue;
};

struct VirtualGeometrySelectResult {
  std::vector<std::uint32_t> visible_meshlet_ids;
  std::vector<IndirectDrawArgs> indirect_args;
};

// Build a simple 2-level hierarchy: leaves = meshlets; root = whole AABB coarse node.
[[nodiscard]] VirtualGeometryAsset BuildVirtualGeometry(std::span<const Vec3> positions,
                                                        std::span<const std::uint32_t> indices,
                                                        int grid_div = 4);

// Screen-error LOD: keep a node if projected error > threshold (pixels); else children.
[[nodiscard]] VirtualGeometrySelectResult SelectClusters(const VirtualGeometryAsset& asset,
                                                         const Mat4& world, const Mat4& view_proj,
                                                         float pixel_error_threshold,
                                                         VirtualGeometryResidency* residency);

// CPU stand-in for GPU meshlet cull CS (writes Indirect args for visible residents).
[[nodiscard]] std::uint32_t CullVirtualGeometryToIndirect(const VirtualGeometryAsset& asset,
                                                          const VirtualGeometrySelectResult& sel,
                                                          const Mat4& world, const Mat4& view_proj,
                                                          std::vector<IndirectDrawArgs>& out_args);

}  // namespace engine::gpu_driven
