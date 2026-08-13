#pragma once

#include "engine/core/math.h"
#include "engine/render/camera.h"

#include <array>
#include <vector>

namespace engine::render {

struct CsmCascade {
  float split = 0.f;  // max view-space depth (along camera forward) covered by this cascade
  Mat4 view_proj = Mat4::Identity();
  int atlas_x = 0;
  int atlas_y = 0;
  int atlas_w = 0;
  int atlas_h = 0;
};

class CascadedShadowMap {
 public:
  void set_cascade_count(int n);
  [[nodiscard]] int cascade_count() const { return count_; }

  // Fit orthographic cascades to camera frustum slices; pack into a square atlas.
  void Build(const Camera& camera, float aspect, const Vec3& light_dir, int atlas_size = 2048,
             float max_shadow_distance = 80.f);

  [[nodiscard]] const std::vector<CsmCascade>& cascades() const { return cascades_; }
  [[nodiscard]] int tiles_per_row() const { return tiles_per_row_; }

  static std::vector<float> ComputeSplits(float z_near, float z_far, int count,
                                          float lambda = 0.5f);

  // World-space corners of a camera frustum slice [near_z, far_z] along view.
  static void FrustumSliceCorners(const Camera& camera, float aspect, float near_z, float far_z,
                                  std::array<Vec3, 8>& out_corners);

 private:
  static Mat4 FitLightMatrix(const Vec3& light_dir, const std::array<Vec3, 8>& corners,
                             int map_resolution);

  int count_ = 3;
  int tiles_per_row_ = 2;
  std::vector<CsmCascade> cascades_;
};

}  // namespace engine::render
