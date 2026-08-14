#include "engine/gi/reflection_probe.h"

#include <algorithm>
#include <cmath>

namespace engine::gi {
namespace {

Vec3 FaceDirection(int face, float u, float v) {
  // u,v in [-1,1]; DX cubemap face order +X -X +Y -Y +Z -Z
  switch (face) {
    case 0:
      return Normalize(Vec3{1.f, -v, -u});
    case 1:
      return Normalize(Vec3{-1.f, -v, u});
    case 2:
      return Normalize(Vec3{u, 1.f, v});
    case 3:
      return Normalize(Vec3{u, -1.f, -v});
    case 4:
      return Normalize(Vec3{u, -v, 1.f});
    default:
      return Normalize(Vec3{-u, -v, -1.f});
  }
}

ColorRgba ShadeSky(const Vec3& dir, const Vec3& sun_dir, const ColorRgba& sun_color,
                   float sun_intensity, const ColorRgba& ambient) {
  const float up = std::clamp(dir.y * 0.5f + 0.5f, 0.f, 1.f);
  ColorRgba sky{ambient.r * (0.55f + 0.45f * up), ambient.g * (0.6f + 0.4f * up),
                ambient.b * (0.75f + 0.35f * up), 1.f};
  const float ndotl = std::max(0.f, Dot(Normalize(dir), Normalize(Vec3{-sun_dir.x, -sun_dir.y, -sun_dir.z})));
  const float sun = std::pow(ndotl, 32.f) * sun_intensity * 0.35f;
  sky.r = std::min(sky.r + sun_color.r * sun, 8.f);
  sky.g = std::min(sky.g + sun_color.g * sun, 8.f);
  sky.b = std::min(sky.b + sun_color.b * sun, 8.f);
  return sky;
}

std::uint8_t ToByte(float x) {
  return static_cast<std::uint8_t>(std::clamp(x, 0.f, 1.f) * 255.f + 0.5f);
}

}  // namespace

void ReflectionProbe::Configure(const Vec3& position, int face_size) {
  position_ = position;
  face_size_ = (std::max)(4, face_size);
  rgba_.assign(static_cast<std::size_t>(6 * face_size_ * face_size_ * 4), 0);
  dirty_ = true;
}

void ReflectionProbe::UpdateFromEnvironment(const Vec3& sun_dir, const ColorRgba& sun_color,
                                            float sun_intensity, const ColorRgba& ambient) {
  if (face_size_ <= 0) {
    Configure(position_, 64);
  }
  rgba_.assign(static_cast<std::size_t>(6 * face_size_ * face_size_ * 4), 0);
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < face_size_; ++y) {
      for (int x = 0; x < face_size_; ++x) {
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(face_size_) * 2.f - 1.f;
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(face_size_) * 2.f - 1.f;
        const Vec3 dir = FaceDirection(face, u, v);
        const ColorRgba c = ShadeSky(dir, sun_dir, sun_color, sun_intensity, ambient);
        // Soft-encode HDR into LDR bytes (simple Reinhard) for upload.
        const float inv = 1.f / (1.f + (c.r + c.g + c.b) * 0.333f);
        const std::size_t i =
            static_cast<std::size_t>(((face * face_size_ + y) * face_size_ + x) * 4);
        rgba_[i + 0] = ToByte(c.r * inv);
        rgba_[i + 1] = ToByte(c.g * inv);
        rgba_[i + 2] = ToByte(c.b * inv);
        rgba_[i + 3] = 255;
      }
    }
  }
  dirty_ = true;
}

ColorRgba ReflectionProbe::SampleDirection(const Vec3& dir) const {
  if (rgba_.empty() || face_size_ <= 0) {
    return {0, 0, 0, 1};
  }
  const Vec3 n = Normalize(dir);
  const float ax = std::fabs(n.x);
  const float ay = std::fabs(n.y);
  const float az = std::fabs(n.z);
  int face = 0;
  float sc = 0.f;
  float tc = 0.f;
  float ma = ax;
  if (ay >= ax && ay >= az) {
    face = n.y > 0.f ? 2 : 3;
    ma = ay;
    sc = n.x;
    tc = n.y > 0.f ? n.z : -n.z;
  } else if (az >= ax && az >= ay) {
    face = n.z > 0.f ? 4 : 5;
    ma = az;
    sc = n.z > 0.f ? n.x : -n.x;
    tc = -n.y;
  } else {
    face = n.x > 0.f ? 0 : 1;
    ma = ax;
    sc = n.x > 0.f ? -n.z : n.z;
    tc = -n.y;
  }
  const float u = 0.5f * (sc / (std::max)(ma, 1e-5f) + 1.f);
  const float v = 0.5f * (tc / (std::max)(ma, 1e-5f) + 1.f);
  const int x = std::clamp(static_cast<int>(u * face_size_), 0, face_size_ - 1);
  const int y = std::clamp(static_cast<int>(v * face_size_), 0, face_size_ - 1);
  const std::size_t i = static_cast<std::size_t>(((face * face_size_ + y) * face_size_ + x) * 4);
  return {rgba_[i] / 255.f, rgba_[i + 1] / 255.f, rgba_[i + 2] / 255.f, 1.f};
}

}  // namespace engine::gi
