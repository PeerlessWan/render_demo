#include "engine/gpu_driven/virtual_geometry.h"

#include <algorithm>
#include <cmath>

namespace engine::gpu_driven {
namespace {

float AabbRadius(const Aabb& a) {
  const float dx = a.max.x - a.min.x;
  const float dy = a.max.y - a.min.y;
  const float dz = a.max.z - a.min.z;
  return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
}

float ProjectedErrorPixels(const Aabb& aabb, const Mat4& world, const Mat4& view_proj,
                           float world_error) {
  const Vec3 c{(aabb.min.x + aabb.max.x) * 0.5f, (aabb.min.y + aabb.max.y) * 0.5f,
               (aabb.min.z + aabb.max.z) * 0.5f};
  const Vec3 wc = world.TransformPoint(c);
  const Vec3 we = world.TransformPoint(Vec3{c.x + world_error, c.y, c.z});
  const auto project = [&](const Vec3& p) {
    const float* m = view_proj.m.data();
    const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    const float iw = (std::fabs(w) > 1e-6f) ? (1.f / w) : 0.f;
    return Vec3{x * iw, y * iw, 0.f};
  };
  const Vec3 a = project(wc);
  const Vec3 b = project(we);
  const float dx = (b.x - a.x) * 0.5f * 1080.f;  // assume ~1080p height scale
  const float dy = (b.y - a.y) * 0.5f * 1080.f;
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

VirtualGeometryAsset BuildVirtualGeometry(std::span<const Vec3> positions,
                                          std::span<const std::uint32_t> indices, int grid_div) {
  VirtualGeometryAsset out;
  out.cook = MeshletizePreferMeshoptimizer(positions, indices, grid_div);
  if (out.cook.meshlets.empty()) {
    return out;
  }

  Aabb root_aabb = out.cook.meshlets[0].aabb;
  for (const auto& m : out.cook.meshlets) {
    root_aabb.min.x = (std::min)(root_aabb.min.x, m.aabb.min.x);
    root_aabb.min.y = (std::min)(root_aabb.min.y, m.aabb.min.y);
    root_aabb.min.z = (std::min)(root_aabb.min.z, m.aabb.min.z);
    root_aabb.max.x = (std::max)(root_aabb.max.x, m.aabb.max.x);
    root_aabb.max.y = (std::max)(root_aabb.max.y, m.aabb.max.y);
    root_aabb.max.z = (std::max)(root_aabb.max.z, m.aabb.max.z);
  }

  ClusterNode root;
  root.parent = -1;
  root.child_begin = 1;
  root.child_count = static_cast<std::int32_t>(out.cook.meshlets.size());
  root.meshlet_index = 0;
  root.error = AabbRadius(root_aabb) * 0.35f;
  root.aabb = root_aabb;
  root.is_leaf = false;
  out.nodes.push_back(root);
  out.root = 0;

  for (std::uint32_t i = 0; i < out.cook.meshlets.size(); ++i) {
    ClusterNode leaf;
    leaf.parent = 0;
    leaf.child_begin = -1;
    leaf.child_count = 0;
    leaf.meshlet_index = i;
    leaf.error = AabbRadius(out.cook.meshlets[i].aabb) * 0.05f;
    leaf.aabb = out.cook.meshlets[i].aabb;
    leaf.is_leaf = true;
    out.nodes.push_back(leaf);
  }
  return out;
}

VirtualGeometrySelectResult SelectClusters(const VirtualGeometryAsset& asset, const Mat4& world,
                                           const Mat4& view_proj, float pixel_error_threshold,
                                           VirtualGeometryResidency* residency) {
  VirtualGeometrySelectResult sel;
  if (asset.nodes.empty()) {
    return sel;
  }
  const float thr = (std::max)(pixel_error_threshold, 0.5f);
  std::vector<std::uint32_t> stack;
  stack.push_back(asset.root);
  while (!stack.empty()) {
    const std::uint32_t ni = stack.back();
    stack.pop_back();
    const ClusterNode& n = asset.nodes[ni];
    const float pix = ProjectedErrorPixels(n.aabb, world, view_proj, n.error);
    if (!n.is_leaf && pix > thr && n.child_count > 0) {
      for (std::int32_t c = 0; c < n.child_count; ++c) {
        stack.push_back(static_cast<std::uint32_t>(n.child_begin + c));
      }
      continue;
    }
    const std::uint32_t mid = n.meshlet_index;
    if (mid >= asset.cook.meshlets.size()) {
      continue;
    }
    if (residency) {
      const auto& res = residency->resident_meshlet_ids;
      if (std::find(res.begin(), res.end(), mid) == res.end()) {
        residency->request_queue.push_back(mid);
        if (residency->resident_meshlet_ids.size() < residency->page_budget) {
          residency->resident_meshlet_ids.push_back(mid);
        } else {
          continue;  // not resident yet
        }
      }
    }
    sel.visible_meshlet_ids.push_back(mid);
  }
  return sel;
}

std::uint32_t CullVirtualGeometryToIndirect(const VirtualGeometryAsset& asset,
                                            const VirtualGeometrySelectResult& sel,
                                            const Mat4& world, const Mat4& view_proj,
                                            std::vector<IndirectDrawArgs>& out_args) {
  out_args.clear();
  std::vector<std::uint32_t> ids;
  std::vector<IndirectDrawArgs> tmp;
  const std::uint32_t n =
      CullMeshletsToIndirect(asset.cook.meshlets, world, view_proj, nullptr, ids, tmp);
  // Intersect with selected meshlets.
  for (std::uint32_t i = 0; i < ids.size(); ++i) {
    if (std::find(sel.visible_meshlet_ids.begin(), sel.visible_meshlet_ids.end(), ids[i]) ==
        sel.visible_meshlet_ids.end()) {
      continue;
    }
    out_args.push_back(tmp[i]);
  }
  (void)n;
  return static_cast<std::uint32_t>(out_args.size());
}

}  // namespace engine::gpu_driven
