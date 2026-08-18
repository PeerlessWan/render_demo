#include "editing/ray.h"

#include <cmath>

namespace editor {

engine::Vec3 AxisDir(Axis axis) {
  switch (axis) {
    case Axis::X:
      return {1.f, 0.f, 0.f};
    case Axis::Y:
      return {0.f, 1.f, 0.f};
    case Axis::Z:
      return {0.f, 0.f, 1.f};
    default:
      return {};
  }
}

Ray ScreenRay(float mouse_x, float mouse_y, float viewport_w, float viewport_h,
              const engine::Mat4& inv_view_proj) {
  Ray r;
  if (viewport_w < 1.f || viewport_h < 1.f) {
    return r;
  }
  const float ndc_x = (mouse_x / viewport_w) * 2.f - 1.f;
  const float ndc_y = 1.f - (mouse_y / viewport_h) * 2.f;
  const engine::Vec3 near_p = inv_view_proj.TransformPoint({ndc_x, ndc_y, 0.f});
  const engine::Vec3 far_p = inv_view_proj.TransformPoint({ndc_x, ndc_y, 1.f});
  engine::Vec3 dir = far_p - near_p;
  if (!std::isfinite(near_p.x) || !std::isfinite(far_p.x) || dir.length_squared() < 1e-12f) {
    r.origin = near_p;
    return r;
  }
  r.origin = near_p;
  r.dir = engine::Normalize(dir);
  return r;
}

float AxisParam(const Ray& ray, const engine::Vec3& origin, Axis axis) {
  const engine::Vec3 a = AxisDir(axis);
  if (a.length_squared() < 1e-12f) {
    return 0.f;
  }
  const engine::Vec3 w0 = ray.origin - origin;
  const float B = engine::Dot(ray.dir, a);
  const float D = engine::Dot(ray.dir, w0);
  const float E = engine::Dot(a, w0);
  const float denom = 1.f - B * B;
  if (std::fabs(denom) < 1e-6f) {
    return E;
  }
  return (E - B * D) / denom;
}

bool HitAxis(const Ray& ray, const engine::Vec3& origin, Axis axis, float length, float radius,
             float* out_dist) {
  const engine::Vec3 a = AxisDir(axis);
  if (a.length_squared() < 1e-12f || length <= 0.f) {
    return false;
  }
  const engine::Vec3 w0 = ray.origin - origin;
  const float B = engine::Dot(ray.dir, a);
  const float D = engine::Dot(ray.dir, w0);
  const float E = engine::Dot(a, w0);
  const float denom = 1.f - B * B;
  float t = 0.f;
  float u = E;
  if (std::fabs(denom) >= 1e-6f) {
    t = (B * E - D) / denom;
    u = (E - B * D) / denom;
  }
  if (u < 0.f) {
    u = 0.f;
  }
  if (u > length) {
    u = length;
  }
  if (t < 0.f) {
    t = 0.f;
  }
  const engine::Vec3 rp = ray.origin + ray.dir * t;
  const engine::Vec3 ap = origin + a * u;
  const float dist = (rp - ap).length();
  if (out_dist) {
    *out_dist = dist;
  }
  return dist <= radius;
}

Axis HitGizmoAxes(const Ray& ray, const engine::Vec3& origin, float length, float radius) {
  Axis best = Axis::None;
  float best_d = radius + 1.f;
  const Axis axes[] = {Axis::X, Axis::Y, Axis::Z};
  for (Axis ax : axes) {
    float d = 0.f;
    if (HitAxis(ray, origin, ax, length, radius, &d) && d < best_d) {
      best_d = d;
      best = ax;
    }
  }
  return best;
}

}  // namespace editor
