#include "engine/gpu_driven/indirect_draw.h"

#include "engine/core/feature.h"

#include <algorithm>

namespace engine::gpu_driven {
namespace {

Aabb TransformAabb(const Aabb& local, const Mat4& world) {
  const Vec3 corners[8] = {
      {local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
      {local.min.x, local.max.y, local.min.z}, {local.max.x, local.max.y, local.min.z},
      {local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
      {local.min.x, local.max.y, local.max.z}, {local.max.x, local.max.y, local.max.z},
  };
  Aabb out;
  out.min = {1e9f, 1e9f, 1e9f};
  out.max = {-1e9f, -1e9f, -1e9f};
  for (const auto& c : corners) {
    const Vec3 w = world.TransformPoint(c);
    out.min.x = std::min(out.min.x, w.x);
    out.min.y = std::min(out.min.y, w.y);
    out.min.z = std::min(out.min.z, w.z);
    out.max.x = std::max(out.max.x, w.x);
    out.max.y = std::max(out.max.y, w.y);
    out.max.z = std::max(out.max.z, w.z);
  }
  return out;
}

float ViewDepthKey(const Mat4& world, const Mat4& view_proj) {
  // Translation column → NDC.z proxy for front-to-back order (M26/C08 deepen).
  const Vec3 t{world.m[12], world.m[13], world.m[14]};
  return view_proj.TransformPoint(t).z;
}

}  // namespace

void FillIndirectArgs(IndirectDrawArgs& out, std::uint32_t index_count,
                      std::uint32_t instance_count) {
  out.index_count_per_instance = index_count;
  out.instance_count = instance_count;
  out.start_index_location = 0;
  out.base_vertex_location = 0;
  out.start_instance_location = 0;
}

std::uint32_t CullInstancesToIndirect(std::span<const Mat4> worlds, std::span<const Aabb> local_bounds,
                                      const Mat4& view_proj, const render::OcclusionBuffer* occ,
                                      std::vector<Mat4>& out_visible, IndirectDrawArgs& out_args,
                                      std::uint32_t index_count_per_instance) {
  // CPU stand-in for a future GPU cull CS that writes IndirectArgs (C08).
  // Improvement: reserve + front-to-back sort of survivors for better early-Z.
  out_visible.clear();
  out_visible.reserve(worlds.size());
  const std::size_t n = worlds.size();
  const Aabb default_local{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
  const Frustum f = Frustum::FromViewProj(view_proj);
  for (std::size_t i = 0; i < n; ++i) {
    const Aabb local = i < local_bounds.size() ? local_bounds[i] : default_local;
    const Aabb world_box = TransformAabb(local, worlds[i]);
    bool visible = true;
    if (occ) {
      visible = occ->IsVisible(world_box, view_proj);
    } else {
      visible = f.ContainsAabb(world_box);
    }
    if (visible) {
      out_visible.push_back(worlds[i]);
    }
  }
  std::sort(out_visible.begin(), out_visible.end(), [&](const Mat4& a, const Mat4& b) {
    return ViewDepthKey(a, view_proj) < ViewDepthKey(b, view_proj);
  });
  FillIndirectArgs(out_args, index_count_per_instance,
                   static_cast<std::uint32_t>(out_visible.size()));
  engine::SetFeatureOverride("execute_indirect", true);
  return out_args.instance_count;
}

bool MeshletPathAvailable() {
  // Default SKIP until meshlet/MS PSOs ship; Feature "meshlet" for host experiments.
  return QueryFeature("meshlet");
}

std::vector<std::uint32_t> PackIndirectArgsU32(const IndirectDrawArgs& args) {
  return {args.index_count_per_instance, args.instance_count, args.start_index_location,
          static_cast<std::uint32_t>(args.base_vertex_location), args.start_instance_location};
}

}  // namespace engine::gpu_driven
