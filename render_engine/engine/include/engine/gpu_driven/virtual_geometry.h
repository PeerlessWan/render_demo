#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/gpu_driven/meshlet.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine::gpu_driven {

// W23/W25 / ADR 0046/0048: Nanite-like VirtualGeometry (NOT UE Nanite).
// Hierarchical cluster DAG + screen-error LOD + residency + GPU-cull / continuous LOD / SW raster.

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
  // W25 continuous LOD: blend weight in [0,1] per visible id (0=parent coarse, 1=child fine).
  std::vector<float> lod_blend;
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

// W25: continuous error interpolation between parent/child (fills lod_blend).
[[nodiscard]] VirtualGeometrySelectResult SelectClustersContinuous(
    const VirtualGeometryAsset& asset, const Mat4& world, const Mat4& view_proj,
    float pixel_error_threshold, VirtualGeometryResidency* residency);

// CPU stand-in for GPU meshlet cull CS (writes Indirect args for visible residents).
[[nodiscard]] std::uint32_t CullVirtualGeometryToIndirect(const VirtualGeometryAsset& asset,
                                                          const VirtualGeometrySelectResult& sel,
                                                          const Mat4& world, const Mat4& view_proj,
                                                          std::vector<IndirectDrawArgs>& out_args);

// W25: GPU cull CS contract (DX+VK L0). CPU reference when no CS; Feature virtual_geometry_gpu_cull.
[[nodiscard]] Status TryDispatchVirtualGeometryCullCs(const VirtualGeometryAsset& asset,
                                                      const VirtualGeometrySelectResult& sel,
                                                      const Mat4& world, const Mat4& view_proj,
                                                      std::vector<IndirectDrawArgs>& out_args);

struct SoftRasterResult {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;  // visibility/coverage tint
  std::uint32_t covered_texels = 0;
};

// W25: micro-triangle SW raster (CPU teaching / CS stand-in). Feature virtual_geometry_sw_raster.
[[nodiscard]] Status SoftRasterizeVirtualGeometry(const VirtualGeometryAsset& asset,
                                                  const VirtualGeometrySelectResult& sel,
                                                  const Mat4& world, const Mat4& view_proj,
                                                  int target_w, int target_h,
                                                  SoftRasterResult& out);

}  // namespace engine::gpu_driven
