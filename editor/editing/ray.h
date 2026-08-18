#pragma once

#include "engine/core/math.h"

namespace editor {

enum class Axis { None = 0, X = 1, Y = 2, Z = 3 };

inline constexpr float kGizmoRingRadius = 0.85f;
inline constexpr float kGizmoRingHit = 0.16f;

struct Ray {
  engine::Vec3 origin{};
  engine::Vec3 dir{0.f, 0.f, -1.f};
};

[[nodiscard]] engine::Vec3 AxisDir(Axis axis);

[[nodiscard]] engine::Vec3 GizmoAxisDir(Axis axis, const engine::Quat& rotation, bool local);

[[nodiscard]] Ray ScreenRay(float mouse_x, float mouse_y, float viewport_w, float viewport_h,
                            const engine::Mat4& inv_view_proj);

[[nodiscard]] float AxisParam(const Ray& ray, const engine::Vec3& origin, Axis axis);
[[nodiscard]] float AxisParamDir(const Ray& ray, const engine::Vec3& origin, const engine::Vec3& axis_dir);

bool HitAxis(const Ray& ray, const engine::Vec3& origin, Axis axis, float length, float radius,
             float* out_dist = nullptr);
bool HitAxisDir(const Ray& ray, const engine::Vec3& origin, const engine::Vec3& axis_dir, float length,
                float radius, float* out_dist = nullptr);

[[nodiscard]] Axis HitGizmoAxes(const Ray& ray, const engine::Vec3& origin, float length,
                                float radius);
[[nodiscard]] Axis HitGizmoAxes(const Ray& ray, const engine::Vec3& origin, float length, float radius,
                                const engine::Quat& rotation, bool local);

bool HitRing(const Ray& ray, const engine::Vec3& origin, Axis axis, float ring_radius,
             float hit_radius, float* out_dist = nullptr);
bool HitRingDir(const Ray& ray, const engine::Vec3& origin, const engine::Vec3& normal, float ring_radius,
                float hit_radius, float* out_dist = nullptr);

[[nodiscard]] Axis HitGizmoRings(const Ray& ray, const engine::Vec3& origin, float ring_radius,
                                 float hit_radius);
[[nodiscard]] Axis HitGizmoRings(const Ray& ray, const engine::Vec3& origin, float ring_radius,
                                 float hit_radius, const engine::Quat& rotation, bool local);

[[nodiscard]] Axis HitGizmoRotate(const Ray& ray, const engine::Vec3& origin, float length,
                                  float axis_radius, float ring_radius, float ring_hit);
[[nodiscard]] Axis HitGizmoRotate(const Ray& ray, const engine::Vec3& origin, float length,
                                  float axis_radius, float ring_radius, float ring_hit,
                                  const engine::Quat& rotation, bool local);

bool RayHitYPlane(const Ray& ray, float y, engine::Vec3* out_hit);

}  // namespace editor
