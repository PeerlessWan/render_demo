#include "engine/core/math.h"

namespace engine {

Quat Quat::FromEulerYxz(float yaw, float pitch, float roll) {
  const float hy = yaw * 0.5f;
  const float hp = pitch * 0.5f;
  const float hr = roll * 0.5f;
  const float cy = std::cos(hy);
  const float sy = std::sin(hy);
  const float cp = std::cos(hp);
  const float sp = std::sin(hp);
  const float cr = std::cos(hr);
  const float sr = std::sin(hr);
  Quat q;
  q.w = cy * cp * cr + sy * sp * sr;
  q.x = cy * sp * cr + sy * cp * sr;
  q.y = sy * cp * cr - cy * sp * sr;
  q.z = cy * cp * sr - sy * sp * cr;
  return q;
}

Vec3 Quat::Rotate(const Vec3& v) const {
  const Vec3 qv{x, y, z};
  const Vec3 t = Cross(qv, v) * 2.f;
  return v + t * w + Cross(qv, t);
}

Mat4 Mat4::Translation(const Vec3& t) {
  Mat4 m = Identity();
  m.m[12] = t.x;
  m.m[13] = t.y;
  m.m[14] = t.z;
  return m;
}

Mat4 Mat4::Scale(const Vec3& s) {
  Mat4 m{};
  m.m = {s.x, 0, 0, 0, 0, s.y, 0, 0, 0, 0, s.z, 0, 0, 0, 0, 1};
  return m;
}

Mat4 Mat4::FromQuat(const Quat& q) {
  const float xx = q.x * q.x;
  const float yy = q.y * q.y;
  const float zz = q.z * q.z;
  const float xy = q.x * q.y;
  const float xz = q.x * q.z;
  const float yz = q.y * q.z;
  const float wx = q.w * q.x;
  const float wy = q.w * q.y;
  const float wz = q.w * q.z;
  Mat4 m{};
  m.m = {1.f - 2.f * (yy + zz),
         2.f * (xy + wz),
         2.f * (xz - wy),
         0,
         2.f * (xy - wz),
         1.f - 2.f * (xx + zz),
         2.f * (yz + wx),
         0,
         2.f * (xz + wy),
         2.f * (yz - wx),
         1.f - 2.f * (xx + yy),
         0,
         0,
         0,
         0,
         1};
  return m;
}

Mat4 Mat4::TRS(const Vec3& t, const Quat& r, const Vec3& s) {
  return Translation(t) * FromQuat(r) * Scale(s);
}

Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
  const Vec3 f = Normalize(target - eye);
  const Vec3 s = Normalize(Cross(f, up));
  const Vec3 u = Cross(s, f);
  Mat4 m{};
  m.m = {s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0, -Dot(s, eye), -Dot(u, eye),
         Dot(f, eye), 1};
  return m;
}

Mat4 Mat4::Perspective(float fovy_rad, float aspect, float z_near, float z_far) {
  const float f = 1.f / std::tan(fovy_rad * 0.5f);
  Mat4 m{};
  m.m = {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (z_far + z_near) / (z_near - z_far), -1, 0, 0,
         (2.f * z_far * z_near) / (z_near - z_far), 0};
  return m;
}

Mat4 Mat4::operator*(const Mat4& o) const {
  Mat4 out{};
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      out.m[c * 4 + r] = m[0 * 4 + r] * o.m[c * 4 + 0] + m[1 * 4 + r] * o.m[c * 4 + 1] +
                         m[2 * 4 + r] * o.m[c * 4 + 2] + m[3 * 4 + r] * o.m[c * 4 + 3];
    }
  }
  return out;
}

Vec3 Mat4::TransformPoint(const Vec3& p) const {
  const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
  const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
  const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
  const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
  if (std::fabs(w) > 1e-8f) {
    return Vec3{x / w, y / w, z / w};
  }
  return Vec3{x, y, z};
}

Vec3 Mat4::TransformVector(const Vec3& v) const {
  return Vec3{m[0] * v.x + m[4] * v.y + m[8] * v.z, m[1] * v.x + m[5] * v.y + m[9] * v.z,
              m[2] * v.x + m[6] * v.y + m[10] * v.z};
}

Frustum Frustum::FromViewProj(const Mat4& vp) {
  Frustum f;
  // Extract planes from clip matrix rows (column-major).
  const auto& m = vp.m;
  auto set = [&](int i, float a, float b, float c, float d) {
    const float len = std::sqrt(a * a + b * b + c * c);
    const float inv = len > 1e-8f ? 1.f / len : 1.f;
    f.planes[static_cast<std::size_t>(i)] = Plane{Vec3{a * inv, b * inv, c * inv}, d * inv};
  };
  // Left, Right, Bottom, Top, Near, Far
  set(0, m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
  set(1, m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
  set(2, m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
  set(3, m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
  set(4, m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]);
  set(5, m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);
  return f;
}

bool Frustum::ContainsAabb(const Aabb& box) const {
  for (const auto& p : planes) {
    const Vec3 positive{p.n.x >= 0 ? box.max.x : box.min.x, p.n.y >= 0 ? box.max.y : box.min.y,
                        p.n.z >= 0 ? box.max.z : box.min.z};
    if (Dot(p.n, positive) + p.d < 0.f) {
      return false;
    }
  }
  return true;
}

}  // namespace engine
