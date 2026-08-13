#include "engine/render/local_lights.h"

#include <algorithm>
#include <cmath>

namespace engine::render {

void LocalLightShadowScheduler::Clear() {
  lights_.clear();
  slots_.clear();
}

void LocalLightShadowScheduler::AddLight(const LocalLight& light) {
  lights_.push_back(light);
}

bool LocalLightShadowScheduler::Pack(ShadowAtlas& atlas) {
  slots_.clear();
  // Larger tiles first for tighter packing.
  std::vector<LocalLight> ordered = lights_;
  std::sort(ordered.begin(), ordered.end(), [](const LocalLight& a, const LocalLight& b) {
    return a.shadow_resolution > b.shadow_resolution;
  });

  bool all_ok = true;
  for (const auto& light : ordered) {
    if (!light.cast_shadows) {
      continue;
    }
    ShadowAtlasSlot slot;
    if (!atlas.Allocate(light.id, light.shadow_resolution, slot)) {
      all_ok = false;
      continue;
    }
    slots_.push_back(slot);
  }
  return all_ok;
}

material::PbrMaterial ResolveMeshMaterial(std::string_view mesh_id) {
  material::PbrMaterial m;
  if (mesh_id == "ground") {
    m.base_color = {0.42f, 0.45f, 0.38f, 1.f};
    m.roughness = 0.9f;
    m.metallic = 0.f;
    m.albedo_tex = "textures/albedo_brick.png";
    m.orm_tex = "textures/orm_brick.png";
  } else if (mesh_id == "metal") {
    m.base_color = {0.9f, 0.9f, 0.92f, 1.f};
    m.roughness = 0.2f;
    m.metallic = 0.85f;
    m.albedo_tex = "textures/albedo_brick.png";
    m.orm_tex = "textures/orm_brick.png";
  } else {
    m.base_color = {0.82f, 0.78f, 0.72f, 1.f};
    m.roughness = 0.35f;
    m.metallic = 0.12f;
    m.albedo_tex = "textures/albedo_brick.png";
    m.orm_tex = "textures/orm_brick.png";
  }
  return m;
}

Mat4 BuildLocalShadowMatrix(const LocalLight& light, const Vec3& look_at) {
  Vec3 target = look_at;
  Vec3 to = target - light.position;
  if (to.length_squared() < 1e-6f) {
    target = light.position + Vec3{0.f, -1.f, 0.f};
  }
  Vec3 up{0.f, 1.f, 0.f};
  const Vec3 forward = Normalize(target - light.position);
  if (std::fabs(Dot(forward, up)) > 0.95f) {
    up = Vec3{0.f, 0.f, 1.f};
  }
  const Mat4 view = Mat4::LookAt(light.position, target, up);
  const float z_far = std::max(light.range, 1.f);
  const Mat4 proj = Mat4::Perspective(1.04719755f, 1.f, 0.05f, z_far);
  return proj * view;
}

}  // namespace engine::render
