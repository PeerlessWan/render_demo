#include "engine/gameplay/possess_controller.h"

#include <algorithm>
#include <cmath>

namespace engine::gameplay {
namespace {

constexpr float kPi = 3.14159265f;

// Match Camera flat axes (FromEulerYxz(yaw,0).Rotate) so walk follows look yaw.
Vec3 YawForward(float yaw) {
  return Normalize(Vec3{-std::sin(yaw), 0.f, -std::cos(yaw)});
}

Vec3 YawRight(float yaw) {
  return Normalize(Vec3{std::cos(yaw), 0.f, -std::sin(yaw)});
}

}  // namespace

CapsuleCharacterMesh BuildCapsuleCharacterMesh(float radius, float height, int rings,
                                               int segments) {
  CapsuleCharacterMesh mesh;
  rings = std::max(2, rings);
  segments = std::max(3, segments);
  const float cyl_h = std::max(0.01f, height - 2.f * radius);
  const float half_cyl = 0.5f * cyl_h;
  const float center_y = radius + half_cyl;
  const int verts_per = segments;
  const int lat_count = rings * 2 + 2;

  for (int lat = 0; lat <= lat_count; ++lat) {
    const float v = static_cast<float>(lat) / static_cast<float>(lat_count);
    float y = 0.f;
    float r = 0.f;
    if (v < 0.25f) {
      const float t = v / 0.25f;
      const float lat_a = -0.5f * kPi + t * (0.5f * kPi);
      y = center_y - half_cyl + radius * std::sin(lat_a);
      r = radius * std::cos(lat_a);
    } else if (v < 0.75f) {
      const float t = (v - 0.25f) / 0.5f;
      y = center_y - half_cyl + t * cyl_h;
      r = radius;
    } else {
      const float t = (v - 0.75f) / 0.25f;
      const float lat_a = t * (0.5f * kPi);
      y = center_y + half_cyl + radius * std::sin(lat_a);
      r = radius * std::cos(lat_a);
    }
    for (int s = 0; s < segments; ++s) {
      const float a = (static_cast<float>(s) / static_cast<float>(segments)) * (2.f * kPi);
      mesh.positions.push_back(Vec3{r * std::cos(a), std::max(0.f, y), r * std::sin(a)});
    }
  }
  for (int lat = 0; lat < lat_count; ++lat) {
    for (int s = 0; s < segments; ++s) {
      const int s1 = (s + 1) % segments;
      const std::uint32_t i0 = static_cast<std::uint32_t>(lat * verts_per + s);
      const std::uint32_t i1 = static_cast<std::uint32_t>(lat * verts_per + s1);
      const std::uint32_t i2 = static_cast<std::uint32_t>((lat + 1) * verts_per + s);
      const std::uint32_t i3 = static_cast<std::uint32_t>((lat + 1) * verts_per + s1);
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);
      mesh.indices.push_back(i3);
    }
  }
  return mesh;
}

float PossessController::GroundY(float x, float z) const {
  if (sample_height_) {
    return sample_height_(x, z);
  }
  return 0.f;
}

float PossessController::CapsuleCenterY() const {
  return position.y + 0.5f * params.capsule_height;
}

Vec3 PossessController::CapsuleCenter() const {
  return Vec3{position.x, CapsuleCenterY(), position.z};
}

Vec3 PossessController::FirstPersonCameraPosition() const {
  return Vec3{position.x, position.y + params.capsule_height * params.eye_height_frac,
              position.z};
}

Vec3 PossessController::ThirdPersonCameraPosition(float yaw) const {
  const Vec3& o = params.camera_offset;
  const float pitch = std::atan2(-std::abs(o.y), (std::max)(std::abs(o.z), 1e-3f));
  return ThirdPersonCameraPosition(yaw, pitch);
}

Vec3 PossessController::ThirdPersonCameraPosition(float yaw, float pitch) const {
  const Vec3 look = ThirdPersonLookAt();
  const Quat q = Quat::FromEulerYxz(yaw, pitch, 0.f);
  const Vec3 forward = q.Rotate(Vec3{0.f, 0.f, -1.f});
  const Vec3 right = q.Rotate(Vec3{1.f, 0.f, 0.f});
  const Vec3& o = params.camera_offset;
  const float dist = std::sqrt(o.y * o.y + o.z * o.z);
  return look - forward * (std::max)(dist, 0.5f) + right * o.x;
}

Vec3 PossessController::ThirdPersonLookAt() const {
  return Vec3{position.x, position.y + params.capsule_height * 0.72f, position.z};
}

void PossessController::ResolveAabbHorizontal() {
  const float rad = params.capsule_radius;
  for (const AabbObstacle& box : obstacles_) {
    const float expanded_min_x = box.min.x - rad;
    const float expanded_max_x = box.max.x + rad;
    const float expanded_min_z = box.min.z - rad;
    const float expanded_max_z = box.max.z + rad;
    const float feet = position.y;
    const float head = position.y + params.capsule_height;
    if (head < box.min.y || feet > box.max.y) {
      continue;
    }
    if (position.x > expanded_min_x && position.x < expanded_max_x && position.z > expanded_min_z &&
        position.z < expanded_max_z) {
      const float dx_l = position.x - expanded_min_x;
      const float dx_r = expanded_max_x - position.x;
      const float dz_l = position.z - expanded_min_z;
      const float dz_r = expanded_max_z - position.z;
      const float m = std::min(std::min(dx_l, dx_r), std::min(dz_l, dz_r));
      if (m == dx_l) {
        position.x = expanded_min_x;
      } else if (m == dx_r) {
        position.x = expanded_max_x;
      } else if (m == dz_l) {
        position.z = expanded_min_z;
      } else {
        position.z = expanded_max_z;
      }
    }
  }
}

void PossessController::StickToGround() {
  const float gy = GroundY(position.x, position.z);
  const float skin = params.ground_skin;
  if (position.y <= gy + skin && velocity.y <= 0.f) {
    position.y = gy;
    velocity.y = 0.f;
    on_ground = true;
  } else {
    on_ground = false;
  }
}

void PossessController::Step(float dt, const PossessInput& input) {
  if (!possess_character) {
    return;
  }
  dt = std::max(0.f, dt);

  const Vec3 forward = YawForward(input.move_yaw);
  const Vec3 right = YawRight(input.move_yaw);
  Vec3 wish = right * input.move_x + forward * input.move_z;
  const float wish_len = wish.length();
  if (wish_len > 1e-4f) {
    wish = wish * (1.f / wish_len);
  }
  velocity.x = wish.x * params.walk_speed;
  velocity.z = wish.z * params.walk_speed;

  if (input.jump && on_ground) {
    velocity.y = params.jump_speed;
    on_ground = false;
  }

  velocity.y -= params.gravity * dt;
  position.x += velocity.x * dt;
  position.z += velocity.z * dt;
  position.y += velocity.y * dt;

  ResolveAabbHorizontal();
  StickToGround();
}

}  // namespace engine::gameplay
