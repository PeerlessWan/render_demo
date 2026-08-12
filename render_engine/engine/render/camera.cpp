#include "engine/render/camera.h"

#include <algorithm>
#include <cmath>

namespace engine::render {

void Camera::AddYawPitch(float dyaw, float dpitch) {
  yaw += dyaw;
  pitch = std::clamp(pitch + dpitch, -1.5f, 1.5f);
}

void Camera::MoveLocal(float forward, float right, float up) {
  const Quat q = Quat::FromEulerYxz(yaw, pitch, 0.f);
  const Vec3 f = q.Rotate(Vec3{0.f, 0.f, -1.f});
  const Vec3 r = q.Rotate(Vec3{1.f, 0.f, 0.f});
  position = position + f * forward + r * right + Vec3{0.f, up, 0.f};
}

Mat4 Camera::view_matrix() const {
  const Quat q = Quat::FromEulerYxz(yaw, pitch, 0.f);
  const Vec3 forward = q.Rotate(Vec3{0.f, 0.f, -1.f});
  return Mat4::LookAt(position, position + forward, Vec3{0.f, 1.f, 0.f});
}

Mat4 Camera::proj_matrix(float aspect) const {
  return Mat4::Perspective(fovy_rad, aspect <= 0.f ? 1.f : aspect, z_near, z_far);
}

Mat4 Camera::view_proj_matrix(float aspect) const { return proj_matrix(aspect) * view_matrix(); }

Frustum Camera::frustum(float aspect) const { return Frustum::FromViewProj(view_proj_matrix(aspect)); }

}  // namespace engine::render
