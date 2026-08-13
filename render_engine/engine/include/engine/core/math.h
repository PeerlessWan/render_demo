#pragma once

#include <array>
#include <cmath>

namespace engine {

struct Vec2 {
  float x = 0.f;
  float y = 0.f;
};

struct Vec3 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;

  [[nodiscard]] float length_squared() const { return x * x + y * y + z * z; }
  [[nodiscard]] float length() const { return std::sqrt(length_squared()); }

  Vec3& operator+=(const Vec3& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
  Vec3& operator*=(float s) {
    x *= s;
    y *= s;
    z *= s;
    return *this;
  }
};

inline Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
inline Vec3 operator-(Vec3 a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return a *= s; }
inline Vec3 operator*(float s, Vec3 a) { return a *= s; }
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 Normalize(Vec3 v) {
  const float len = v.length();
  return len > 1e-8f ? v * (1.f / len) : Vec3{};
}

struct Vec4 {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float w = 0.f;
};

struct ColorRgba {
  float r = 0.f;
  float g = 0.f;
  float b = 0.f;
  float a = 1.f;
};

struct Quat {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float w = 1.f;

  static Quat Identity() { return {}; }
  static Quat FromEulerYxz(float yaw, float pitch, float roll);
  [[nodiscard]] Vec3 Rotate(const Vec3& v) const;
};

// Column-major 4x4.
struct Mat4 {
  std::array<float, 16> m{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
  };

  static Mat4 Identity() { return {}; }
  static Mat4 Translation(const Vec3& t);
  static Mat4 Scale(const Vec3& s);
  static Mat4 FromQuat(const Quat& q);
  static Mat4 TRS(const Vec3& t, const Quat& r, const Vec3& s);
  static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
  static Mat4 Perspective(float fovy_rad, float aspect, float z_near, float z_far);
  static Mat4 Orthographic(float left, float right, float bottom, float top, float z_near,
                           float z_far);

  Mat4 operator*(const Mat4& o) const;
  [[nodiscard]] Mat4 Inverse() const;
  [[nodiscard]] Vec3 TransformPoint(const Vec3& p) const;
  [[nodiscard]] Vec3 TransformVector(const Vec3& v) const;
};

struct Aabb {
  Vec3 min{0, 0, 0};
  Vec3 max{0, 0, 0};
  [[nodiscard]] Vec3 center() const { return (min + max) * 0.5f; }
  [[nodiscard]] Vec3 extents() const { return (max - min) * 0.5f; }
};

struct Plane {
  Vec3 n{0, 1, 0};
  float d = 0.f;  // n·x + d = 0 with |n|=1 preferred
};

struct Frustum {
  std::array<Plane, 6> planes{};
  static Frustum FromViewProj(const Mat4& view_proj);
  [[nodiscard]] bool ContainsAabb(const Aabb& box) const;
};

}  // namespace engine
