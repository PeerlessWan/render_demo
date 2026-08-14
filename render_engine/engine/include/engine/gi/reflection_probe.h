#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <vector>

namespace engine::gi {

// M13: dynamic reflection probe (CPU faces → GPU cubemap upload).
class ReflectionProbe {
 public:
  void Configure(const Vec3& position, int face_size = 64);
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] const Vec3& position() const { return position_; }
  [[nodiscard]] int face_size() const { return face_size_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  void ClearDirty() { dirty_ = false; }

  // Rebuild 6 RGBA8 faces from sun/ambient (dynamic env stand-in until full scene capture).
  void UpdateFromEnvironment(const Vec3& sun_dir, const ColorRgba& sun_color, float sun_intensity,
                             const ColorRgba& ambient);

  // face-major: 6 * face_size * face_size * 4 bytes.
  [[nodiscard]] const std::vector<std::uint8_t>& rgba_faces() const { return rgba_; }

  // Sample specular-ish color for unit tests (no GPU).
  [[nodiscard]] ColorRgba SampleDirection(const Vec3& dir) const;

 private:
  bool enabled_ = true;
  bool dirty_ = true;
  Vec3 position_{};
  int face_size_ = 64;
  std::vector<std::uint8_t> rgba_;
};

}  // namespace engine::gi
