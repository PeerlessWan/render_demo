#include "engine/render/render_scene.h"

#include <algorithm>

namespace engine::render {
namespace {

Aabb TransformAabb(const Aabb& local, const Mat4& world) {
  const Vec3 corners[8] = {
      {local.min.x, local.min.y, local.min.z}, {local.max.x, local.min.y, local.min.z},
      {local.min.x, local.max.y, local.min.z}, {local.max.x, local.max.y, local.min.z},
      {local.min.x, local.min.y, local.max.z}, {local.max.x, local.min.y, local.max.z},
      {local.min.x, local.max.y, local.max.z}, {local.max.x, local.max.y, local.max.z},
  };
  Aabb out;
  out.min = out.max = world.TransformPoint(corners[0]);
  for (int i = 1; i < 8; ++i) {
    const Vec3 p = world.TransformPoint(corners[i]);
    out.min.x = std::min(out.min.x, p.x);
    out.min.y = std::min(out.min.y, p.y);
    out.min.z = std::min(out.min.z, p.z);
    out.max.x = std::max(out.max.x, p.x);
    out.max.y = std::max(out.max.y, p.y);
    out.max.z = std::max(out.max.z, p.z);
  }
  return out;
}

void Visit(const scene::World& world, scene::NodeId id, const Frustum& frustum, RenderScene& out) {
  if (!world.valid(id) || !world.visible(id)) {
    return;
  }
  if (const auto* mesh = world.mesh(id)) {
    if (mesh->visible) {
      const Mat4& wm = world.world_matrix(id);
      const Aabb wb = TransformAabb(mesh->local_bounds, wm);
      if (mesh->never_cull || frustum.ContainsAabb(wb)) {
        RenderInstance inst;
        inst.node = id;
        inst.world = wm;
        inst.world_bounds = wb;
        inst.mesh_id = mesh->mesh_id;
        out.instances.push_back(std::move(inst));
      } else {
        ++out.culled;
      }
    }
  } else if (world.light(id) || world.camera(id) || world.collider(id) || world.sprite(id)) {
    const Mat4& wm = world.world_matrix(id);
    Aabb local{{-0.25f, -0.25f, -0.25f}, {0.25f, 0.25f, 0.25f}};
    if (world.sprite(id)) {
      local = {{-0.5f, -0.05f, -0.5f}, {0.5f, 0.15f, 0.5f}};
    }
    if (const auto* col = world.collider(id)) {
      local.min = {-col->hx, -col->hy, -col->hz};
      local.max = {col->hx, col->hy, col->hz};
    }
    RenderInstance inst;
    inst.node = id;
    inst.world = wm;
    inst.world_bounds = TransformAabb(local, wm);
    inst.mesh_id.clear();
    out.instances.push_back(std::move(inst));
  }
  for (scene::NodeId c : world.children(id)) {
    Visit(world, c, frustum, out);
  }
}

}  // namespace

RenderScene RenderSceneExtractor::Extract(const scene::World& world, const Camera& camera,
                                          float aspect) {
  RenderScene scene;
  scene.camera = camera;
  const Frustum frustum = camera.frustum(aspect);
  for (scene::NodeId r : world.roots()) {
    Visit(world, r, frustum, scene);
  }
  return scene;
}

}  // namespace engine::render
