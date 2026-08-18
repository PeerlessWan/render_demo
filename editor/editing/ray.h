#pragma once

#include "engine/core/math.h"

namespace editor {

enum class Axis { None = 0, X = 1, Y = 2, Z = 3 };

struct Ray {
  engine::Vec3 origin{};
  engine::Vec3 dir{0.f, 0.f, -1.f};
};

[[nodiscard]] engine::Vec3 AxisDir(Axis axis);

[[nodiscard]] Ray ScreenRay(float mouse_x, float mouse_y, float viewport_w, float viewport_h,
                            const engine::Mat4& inv_view_proj);

// Signed distance along the axis from `origin` to the closest point with the ray.
[[nodiscard]] float AxisParam(const Ray& ray, const engine::Vec3& origin, Axis axis);

// Segment [origin, origin+dir*length]. Returns true when the ray passes within `radius`.
bool HitAxis(const Ray& ray, const engine::Vec3& origin, Axis axis, float length, float radius,
             float* out_dist = nullptr);

[[nodiscard]] Axis HitGizmoAxes(const Ray& ray, const engine::Vec3& origin, float length,
                                float radius);

}  // namespace editor
