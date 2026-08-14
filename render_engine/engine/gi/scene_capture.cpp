#include "engine/gi/scene_capture.h"

#include <algorithm>
#include <cmath>

namespace engine::gi {
namespace {

Vec3 FaceDir(int face, float u, float v) {
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

std::uint8_t ToByte(float x) {
  return static_cast<std::uint8_t>(std::clamp(x, 0.f, 1.f) * 255.f + 0.5f);
}

}  // namespace

void CaptureApproximateSceneFaces(std::vector<std::uint8_t>& rgba_faces, int face_size,
                                  const Vec3& probe_pos, std::span<const SceneCaptureOrb> orbs,
                                  const Vec3& sun_dir, const ColorRgba& sun_color, float sun_intensity,
                                  const ColorRgba& ambient) {
  face_size = (std::max)(4, face_size);
  rgba_faces.assign(static_cast<std::size_t>(6 * face_size * face_size * 4), 0);
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < face_size; ++y) {
      for (int x = 0; x < face_size; ++x) {
        const float u = (x + 0.5f) / face_size * 2.f - 1.f;
        const float v = (y + 0.5f) / face_size * 2.f - 1.f;
        const Vec3 dir = FaceDir(face, u, v);
        float r = ambient.r * 0.6f;
        float g = ambient.g * 0.65f;
        float b = ambient.b * 0.8f;
        const float sun =
            std::pow(std::max(0.f, Dot(dir, Normalize(Vec3{-sun_dir.x, -sun_dir.y, -sun_dir.z}))),
                     24.f) *
            sun_intensity * 0.25f;
        r += sun_color.r * sun;
        g += sun_color.g * sun;
        b += sun_color.b * sun;
        for (const auto& orb : orbs) {
          const Vec3 to = Normalize(orb.position - probe_pos);
          const float ang = std::max(0.f, Dot(dir, to));
          const float fall = std::pow(ang, 32.f) * (orb.radius / (orb.radius + 0.25f));
          r += orb.color.r * fall;
          g += orb.color.g * fall;
          b += orb.color.b * fall;
        }
        const float inv = 1.f / (1.f + (r + g + b) * 0.25f);
        const std::size_t i =
            static_cast<std::size_t>(((face * face_size + y) * face_size + x) * 4);
        rgba_faces[i + 0] = ToByte(r * inv);
        rgba_faces[i + 1] = ToByte(g * inv);
        rgba_faces[i + 2] = ToByte(b * inv);
        rgba_faces[i + 3] = 255;
      }
    }
  }
}

Mat4 ProbeFaceViewProj(const Vec3& probe_pos, int face, float near_z, float far_z) {
  Vec3 forward{};
  Vec3 up{0.f, 1.f, 0.f};
  switch (face) {
    case 0:
      forward = {1.f, 0.f, 0.f};
      up = {0.f, -1.f, 0.f};
      break;
    case 1:
      forward = {-1.f, 0.f, 0.f};
      up = {0.f, -1.f, 0.f};
      break;
    case 2:
      forward = {0.f, 1.f, 0.f};
      up = {0.f, 0.f, 1.f};
      break;
    case 3:
      forward = {0.f, -1.f, 0.f};
      up = {0.f, 0.f, -1.f};
      break;
    case 4:
      forward = {0.f, 0.f, 1.f};
      up = {0.f, -1.f, 0.f};
      break;
    default:
      forward = {0.f, 0.f, -1.f};
      up = {0.f, -1.f, 0.f};
      break;
  }
  const Mat4 view = Mat4::LookAt(probe_pos, probe_pos + forward, up);
  const Mat4 proj = Mat4::Perspective(1.57079632679f, 1.f, near_z, far_z);
  return proj * view;
}

}  // namespace engine::gi
