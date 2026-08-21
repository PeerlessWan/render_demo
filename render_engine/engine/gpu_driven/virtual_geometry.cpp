#include "engine/gpu_driven/virtual_geometry.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

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
  const float dx = (b.x - a.x) * 0.5f * 1080.f;
  const float dy = (b.y - a.y) * 0.5f * 1080.f;
  return std::sqrt(dx * dx + dy * dy);
}

bool EnsureResident(VirtualGeometryResidency* residency, std::uint32_t mid) {
  if (!residency) {
    return true;
  }
  const auto& res = residency->resident_meshlet_ids;
  if (std::find(res.begin(), res.end(), mid) != res.end()) {
    return true;
  }
  residency->request_queue.push_back(mid);
  if (residency->resident_meshlet_ids.size() < residency->page_budget) {
    residency->resident_meshlet_ids.push_back(mid);
    return true;
  }
  return false;
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
    if (!EnsureResident(residency, mid)) {
      continue;
    }
    sel.visible_meshlet_ids.push_back(mid);
    sel.lod_blend.push_back(n.is_leaf ? 1.f : 0.f);
  }
  return sel;
}

VirtualGeometrySelectResult SelectClustersContinuous(const VirtualGeometryAsset& asset,
                                                     const Mat4& world, const Mat4& view_proj,
                                                     float pixel_error_threshold,
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
    if (!n.is_leaf && n.child_count > 0) {
      // Continuous: when near threshold, emit parent + children with blend weights.
      if (pix > thr * 1.25f) {
        for (std::int32_t c = 0; c < n.child_count; ++c) {
          stack.push_back(static_cast<std::uint32_t>(n.child_begin + c));
        }
        continue;
      }
      if (pix > thr * 0.75f) {
        const float t = (pix - thr * 0.75f) / (thr * 0.5f);
        const float child_w = std::clamp(t, 0.f, 1.f);
        const float parent_w = 1.f - child_w;
        const std::uint32_t pmid = n.meshlet_index;
        if (pmid < asset.cook.meshlets.size() && EnsureResident(residency, pmid) && parent_w > 0.05f) {
          sel.visible_meshlet_ids.push_back(pmid);
          sel.lod_blend.push_back(parent_w * 0.35f);  // coarse weight
        }
        for (std::int32_t c = 0; c < n.child_count; ++c) {
          const auto& ch = asset.nodes[static_cast<std::size_t>(n.child_begin + c)];
          if (ch.meshlet_index >= asset.cook.meshlets.size()) {
            continue;
          }
          if (!EnsureResident(residency, ch.meshlet_index)) {
            continue;
          }
          sel.visible_meshlet_ids.push_back(ch.meshlet_index);
          sel.lod_blend.push_back(child_w);
        }
        continue;
      }
      // Far: keep coarse parent only.
    }
    const std::uint32_t mid = n.meshlet_index;
    if (mid >= asset.cook.meshlets.size()) {
      continue;
    }
    if (!EnsureResident(residency, mid)) {
      continue;
    }
    sel.visible_meshlet_ids.push_back(mid);
    sel.lod_blend.push_back(n.is_leaf ? 1.f : 0.f);
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

Status TryDispatchVirtualGeometryCullCs(const VirtualGeometryAsset& asset,
                                        const VirtualGeometrySelectResult& sel, const Mat4& world,
                                        const Mat4& view_proj,
                                        std::vector<IndirectDrawArgs>& out_args) {
  if (!QueryFeature("virtual_geometry") && !QueryFeature("virtual_geometry_gpu_cull")) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryDispatchVirtualGeometryCullCs SKIP: Feature off");
  }
  SetFeatureOverride("virtual_geometry_gpu_cull", true);
  const std::uint32_t n = CullVirtualGeometryToIndirect(asset, sel, world, view_proj, out_args);
  LogInfo("TryDispatchVirtualGeometryCullCs: Ok indirect=" + std::to_string(n) +
          " (CPU CS contract; DX+VK L0)");
  return Status::Ok("vg-gpu-cull-cs");
}

Status SoftRasterizeVirtualGeometry(const VirtualGeometryAsset& asset,
                                    const VirtualGeometrySelectResult& sel, const Mat4& world,
                                    const Mat4& view_proj, int target_w, int target_h,
                                    SoftRasterResult& out) {
  out = {};
  if (!QueryFeature("virtual_geometry") && !QueryFeature("virtual_geometry_sw_raster")) {
    return Status::Fail(ErrorCode::Unavailable, "SoftRasterizeVirtualGeometry SKIP: Feature off");
  }
  if (target_w <= 0 || target_h <= 0) {
    return Status::Fail(ErrorCode::InvalidArgument, "SoftRasterizeVirtualGeometry: bad target");
  }
  SetFeatureOverride("virtual_geometry_sw_raster", true);
  out.width = target_w;
  out.height = target_h;
  out.rgba.assign(static_cast<std::size_t>(target_w) * static_cast<std::size_t>(target_h) * 4u, 0);
  out.covered_texels = 0;

  const auto project = [&](const Vec3& p) {
    const Vec3 wp = world.TransformPoint(p);
    const float* m = view_proj.m.data();
    const float x = m[0] * wp.x + m[4] * wp.y + m[8] * wp.z + m[12];
    const float y = m[1] * wp.x + m[5] * wp.y + m[9] * wp.z + m[13];
    const float w = m[3] * wp.x + m[7] * wp.y + m[11] * wp.z + m[15];
    const float iw = (std::fabs(w) > 1e-6f) ? (1.f / w) : 0.f;
    return Vec3{(x * iw * 0.5f + 0.5f) * static_cast<float>(target_w),
                (1.f - (y * iw * 0.5f + 0.5f)) * static_cast<float>(target_h), 0.f};
  };

  auto plot = [&](int px, int py, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (px < 0 || py < 0 || px >= target_w || py >= target_h) {
      return;
    }
    const std::size_t i =
        (static_cast<std::size_t>(py) * static_cast<std::size_t>(target_w) +
         static_cast<std::size_t>(px)) *
        4u;
    if (out.rgba[i + 3] == 0) {
      ++out.covered_texels;
    }
    out.rgba[i + 0] = r;
    out.rgba[i + 1] = g;
    out.rgba[i + 2] = b;
    out.rgba[i + 3] = 255;
  };

  // Micro-triangle SW raster: splat AABB corners of selected meshlets (teaching visibility).
  for (std::size_t i = 0; i < sel.visible_meshlet_ids.size(); ++i) {
    const std::uint32_t mid = sel.visible_meshlet_ids[i];
    if (mid >= asset.cook.meshlets.size()) {
      continue;
    }
    const Aabb& a = asset.cook.meshlets[mid].aabb;
    const Vec3 corners[8] = {
        {a.min.x, a.min.y, a.min.z}, {a.max.x, a.min.y, a.min.z}, {a.min.x, a.max.y, a.min.z},
        {a.max.x, a.max.y, a.min.z}, {a.min.x, a.min.y, a.max.z}, {a.max.x, a.min.y, a.max.z},
        {a.min.x, a.max.y, a.max.z}, {a.max.x, a.max.y, a.max.z},
    };
    const float blend = (i < sel.lod_blend.size()) ? sel.lod_blend[i] : 1.f;
    const auto r = static_cast<std::uint8_t>(40 + static_cast<int>(blend * 180.f));
    const auto g = static_cast<std::uint8_t>(80 + (mid % 8) * 12);
    const auto b = static_cast<std::uint8_t>(120);
    int minx = target_w, miny = target_h, maxx = 0, maxy = 0;
    for (const auto& c : corners) {
      const Vec3 s = project(c);
      const int px = static_cast<int>(s.x);
      const int py = static_cast<int>(s.y);
      minx = (std::min)(minx, px);
      miny = (std::min)(miny, py);
      maxx = (std::max)(maxx, px);
      maxy = (std::max)(maxy, py);
    }
    minx = (std::max)(0, minx);
    miny = (std::max)(0, miny);
    maxx = (std::min)(target_w - 1, maxx);
    maxy = (std::min)(target_h - 1, maxy);
    // Fill coarse bbox as micro-tri coverage stand-in.
    for (int y = miny; y <= maxy; ++y) {
      for (int x = minx; x <= maxx; ++x) {
        plot(x, y, r, g, b);
      }
    }
  }
  LogInfo("SoftRasterizeVirtualGeometry: Ok covered=" + std::to_string(out.covered_texels));
  return Status::Ok("vg-sw-raster");
}

}  // namespace engine::gpu_driven
