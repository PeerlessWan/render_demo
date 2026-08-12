#pragma once

#include "engine/core/math.h"

namespace engine::render {

struct Camera {
  Vec3 position{0.f, 1.5f, 4.f};
  float yaw = 0.f;
  float pitch = 0.f;
  float fovy_rad = 1.04719755f;  // 60 deg
  float z_near = 0.1f;
  float z_far = 500.f;

  void AddYawPitch(float dyaw, float dpitch);
  void MoveLocal(float forward, float right, float up);

  [[nodiscard]] Mat4 view_matrix() const;
  [[nodiscard]] Mat4 proj_matrix(float aspect) const;
  [[nodiscard]] Mat4 view_proj_matrix(float aspect) const;
  [[nodiscard]] Frustum frustum(float aspect) const;
};

}  // namespace engine::render
