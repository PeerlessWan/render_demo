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

engine::Vec3 GizmoAxisDir(Axis axis, const engine::Quat& rotation, bool local) {
  const engine::Vec3 d = AxisDir(axis);
  if (!local) {
    return d;
  }
  return rotation.Rotate(d);
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

float AxisParamDir(const Ray& ray, const engine::Vec3& origin, const engine::Vec3& axis_dir) {
  engine::Vec3 a = axis_dir;
  if (a.length_squared() < 1e-12f) {
    return 0.f;
  }
  a = engine::Normalize(a);
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

float AxisParam(const Ray& ray, const engine::Vec3& origin, Axis axis) {
  return AxisParamDir(ray, origin, AxisDir(axis));
}

bool HitAxisDir(const Ray& ray, const engine::Vec3& origin, const engine::Vec3& axis_dir, float length,
                float radius, float* out_dist) {
  engine::Vec3 a = axis_dir;
  if (a.length_squared() < 1e-12f || length <= 0.f) {
    return false;
  }
  a = engine::Normalize(a);
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

bool HitAxis(const Ray& ray, const engine::Vec3& origin, Axis axis, float length, float radius,
             float* out_dist) {
  return HitAxisDir(ray, origin, AxisDir(axis), length, radius, out_dist);
}

Axis HitGizmoAxes(const Ray& ray, const engine::Vec3& origin, float length, float radius) {
  return HitGizmoAxes(ray, origin, length, radius, engine::Quat::Identity(), false);
}

Axis HitGizmoAxes(const Ray& ray, const engine::Vec3& origin, float length, float radius,
                  const engine::Quat& rotation, bool local) {
  Axis best = Axis::None;
  float best_d = radius + 1.f;
  const Axis axes[] = {Axis::X, Axis::Y, Axis::Z};
  for (Axis ax : axes) {
    float d = 0.f;
    if (HitAxisDir(ray, origin, GizmoAxisDir(ax, rotation, local), length, radius, &d) && d < best_d) {
      best_d = d;
      best = ax;
    }
  }
  return best;
}

bool HitRingDir(const Ray& ray, const engine::Vec3& origin, const engine::Vec3& normal, float ring_radius,
                float hit_radius, float* out_dist) {
  engine::Vec3 n = normal;
  if (n.length_squared() < 1e-12f) {
    return false;
  }
  n = engine::Normalize(n);
  const float nd = engine::Dot(ray.dir, n);
  if (std::fabs(nd) < 1e-6f) {
    return false;
  }
  const float t = engine::Dot(origin - ray.origin, n) / nd;
  if (t < 0.f) {
    return false;
  }
  const engine::Vec3 p = ray.origin + ray.dir * t;
  engine::Vec3 radial = p - origin;
  radial = radial - n * engine::Dot(radial, n);
  const float len = radial.length();
  if (len < 1e-6f) {
    return false;
  }
  const engine::Vec3 q = origin + radial * (ring_radius / len);
  const float dist = (p - q).length();
  if (out_dist) {
    *out_dist = dist;
  }
  return dist <= hit_radius;
}

bool HitRing(const Ray& ray, const engine::Vec3& origin, Axis axis, float ring_radius,
             float hit_radius, float* out_dist) {
  return HitRingDir(ray, origin, AxisDir(axis), ring_radius, hit_radius, out_dist);
}

Axis HitGizmoRings(const Ray& ray, const engine::Vec3& origin, float ring_radius, float hit_radius) {
  return HitGizmoRings(ray, origin, ring_radius, hit_radius, engine::Quat::Identity(), false);
}

Axis HitGizmoRings(const Ray& ray, const engine::Vec3& origin, float ring_radius, float hit_radius,
                   const engine::Quat& rotation, bool local) {
  Axis best = Axis::None;
  float best_d = hit_radius + 1.f;
  const Axis axes[] = {Axis::X, Axis::Y, Axis::Z};
  for (Axis ax : axes) {
    float d = 0.f;
    if (HitRingDir(ray, origin, GizmoAxisDir(ax, rotation, local), ring_radius, hit_radius, &d) &&
        d < best_d) {
      best_d = d;
      best = ax;
    }
  }
  return best;
}

Axis HitGizmoRotate(const Ray& ray, const engine::Vec3& origin, float length, float axis_radius,
                    float ring_radius, float ring_hit) {
  return HitGizmoRotate(ray, origin, length, axis_radius, ring_radius, ring_hit, engine::Quat::Identity(),
                        false);
}

Axis HitGizmoRotate(const Ray& ray, const engine::Vec3& origin, float length, float axis_radius,
                    float ring_radius, float ring_hit, const engine::Quat& rotation, bool local) {
  const Axis ring = HitGizmoRings(ray, origin, ring_radius, ring_hit, rotation, local);
  if (ring != Axis::None) {
    return ring;
  }
  return HitGizmoAxes(ray, origin, length, axis_radius, rotation, local);
}

bool RayHitYPlane(const Ray& ray, float y, engine::Vec3* out_hit) {
  if (std::fabs(ray.dir.y) < 1e-6f) {
    return false;
  }
  const float t = (y - ray.origin.y) / ray.dir.y;
  if (t < 0.f) {
    return false;
  }
  const engine::Vec3 p = ray.origin + ray.dir * t;
  if (!std::isfinite(p.x) || !std::isfinite(p.z)) {
    return false;
  }
  if (out_hit) {
    *out_hit = p;
  }
  return true;
}

}  // namespace editor
