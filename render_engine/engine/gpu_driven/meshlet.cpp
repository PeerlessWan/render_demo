#include "engine/gpu_driven/meshlet.h"

#include "engine/core/feature.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

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

Aabb Expand(Aabb a, const Vec3& p) {
  a.min.x = std::min(a.min.x, p.x);
  a.min.y = std::min(a.min.y, p.y);
  a.min.z = std::min(a.min.z, p.z);
  a.max.x = std::max(a.max.x, p.x);
  a.max.y = std::max(a.max.y, p.y);
  a.max.z = std::max(a.max.z, p.z);
  return a;
}

}  // namespace

MeshletCookResult MeshletizeAabbGrid(std::span<const Vec3> positions,
                                     std::span<const std::uint32_t> indices, int grid_div) {
  MeshletCookResult out;
  if (positions.empty() || indices.size() < 3) {
    return out;
  }

  Aabb mesh_aabb{positions[0], positions[0]};
  for (const auto& p : positions) {
    mesh_aabb = Expand(mesh_aabb, p);
  }
  const Vec3 ext = mesh_aabb.extents();
  const Vec3 size{std::max(ext.x * 2.f, 1e-6f), std::max(ext.y * 2.f, 1e-6f),
                  std::max(ext.z * 2.f, 1e-6f)};

  const int div = std::max(grid_div, 1);
  struct Bin {
    std::vector<std::uint32_t> tris;  // flat i0,i1,i2
    Aabb aabb{};
    bool has = false;
  };
  std::unordered_map<int, Bin> bins;

  const std::size_t tri_count = indices.size() / 3;
  for (std::size_t t = 0; t < tri_count; ++t) {
    const std::uint32_t i0 = indices[t * 3 + 0];
    const std::uint32_t i1 = indices[t * 3 + 1];
    const std::uint32_t i2 = indices[t * 3 + 2];
    if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) {
      continue;
    }
    const Vec3 c = (positions[i0] + positions[i1] + positions[i2]) * (1.f / 3.f);
    int cx = static_cast<int>((c.x - mesh_aabb.min.x) / size.x * static_cast<float>(div));
    int cy = static_cast<int>((c.y - mesh_aabb.min.y) / size.y * static_cast<float>(div));
    int cz = static_cast<int>((c.z - mesh_aabb.min.z) / size.z * static_cast<float>(div));
    cx = std::clamp(cx, 0, div - 1);
    cy = std::clamp(cy, 0, div - 1);
    cz = std::clamp(cz, 0, div - 1);
    const int key = (cx * div + cy) * div + cz;
    auto& bin = bins[key];
    if (!bin.has) {
      bin.aabb = {positions[i0], positions[i0]};
      bin.has = true;
    }
    bin.aabb = Expand(bin.aabb, positions[i0]);
    bin.aabb = Expand(bin.aabb, positions[i1]);
    bin.aabb = Expand(bin.aabb, positions[i2]);
    bin.tris.push_back(i0);
    bin.tris.push_back(i1);
    bin.tris.push_back(i2);
  }

  out.meshlets.reserve(bins.size());
  for (auto& [key, bin] : bins) {
    (void)key;
    if (bin.tris.empty()) {
      continue;
    }
    Meshlet m;
    m.first_index = static_cast<std::uint32_t>(out.indices.size());
    m.index_count = static_cast<std::uint32_t>(bin.tris.size());
    m.first_vertex = 0;
    m.vertex_count = static_cast<std::uint32_t>(positions.size());
    m.aabb = bin.aabb;
    out.indices.insert(out.indices.end(), bin.tris.begin(), bin.tris.end());
    out.meshlets.push_back(m);
  }
  return out;
}

std::uint32_t CullMeshletsToIndirect(std::span<const Meshlet> meshlets, const Mat4& world,
                                     const Mat4& view_proj, const render::OcclusionBuffer* occ,
                                     std::vector<std::uint32_t>& out_visible_ids,
                                     std::vector<IndirectDrawArgs>& out_args) {
  out_visible_ids.clear();
  out_args.clear();
  out_visible_ids.reserve(meshlets.size());
  out_args.reserve(meshlets.size());

  const Frustum f = Frustum::FromViewProj(view_proj);
  for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(meshlets.size()); ++i) {
    const Aabb world_box = TransformAabb(meshlets[i].aabb, world);
    bool visible = true;
    if (occ) {
      visible = occ->IsVisible(world_box, view_proj);
    } else {
      visible = f.ContainsAabb(world_box);
    }
    if (!visible) {
      continue;
    }
    out_visible_ids.push_back(i);
    IndirectDrawArgs args;
    FillIndirectArgs(args, meshlets[i].index_count, 1);
    args.start_index_location = meshlets[i].first_index;
    args.base_vertex_location = static_cast<std::int32_t>(meshlets[i].first_vertex);
    out_args.push_back(args);
  }
  engine::SetFeatureOverride("execute_indirect", true);
  return static_cast<std::uint32_t>(out_visible_ids.size());
}

Status TryMeshShaderPathStub() {
  if (!MeshletPathAvailable()) {
    return Status::Fail(ErrorCode::Unavailable,
                        "Mesh Shader path SKIP: Feature meshlet=false (C08)");
  }
  // Real amplification/mesh PSO not shipped this wave — Feature ON only means cull path OK.
  return Status::Fail(ErrorCode::Unavailable,
                      "Mesh Shader PSO stub: Feature meshlet on but MS PSO not shipped (C08 cull OK)");
}

}  // namespace engine::gpu_driven
