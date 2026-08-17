#pragma once

#include "engine/core/math.h"
#include "engine/terrain/heightmap.h"

#include <cstdint>
#include <vector>

namespace engine::ocean {

// Mega-W8 / C09: CPU radix-2 FFT heightfield ocean with camera-snapped infinite tiling.
struct FftOceanDesc {
  int resolution = 64;   // power-of-two: 64 or 128
  float world_size = 64.f;
  float amplitude = 0.45f;
  float wind_speed = 12.f;
  Vec2 wind_dir{1.f, 0.25f};
  float chop = 0.85f;  // horizontal displacement scale (visual)
};

class FftOcean {
 public:
  void Configure(const FftOceanDesc& desc);
  void Update(float dt);
  // Snap tile origin so the heightfield patch follows the camera on XZ.
  void SnapOriginToCamera(const Vec3& camera_pos);

  [[nodiscard]] float SampleHeight(float x, float z) const;
  [[nodiscard]] Vec3 SampleNormal(float x, float z) const;
  [[nodiscard]] float SampleFoam(float x, float z) const;  // slope-based foam [0,1]
  [[nodiscard]] Vec3 origin() const { return origin_; }
  [[nodiscard]] float world_size() const { return world_size_; }
  [[nodiscard]] int resolution() const { return n_; }
  [[nodiscard]] float time() const { return time_; }
  [[nodiscard]] const terrain::Heightmap& heightfield() const { return height_; }
  [[nodiscard]] const std::vector<float>& foam() const { return foam_; }

  // Displace an existing water grid mesh (world XZ kept; Y + normals + optional UV.x foam).
  void AnimateMesh(terrain::TerrainMesh& mesh) const;

 private:
  int n_ = 0;
  float world_size_ = 64.f;
  float amplitude_ = 0.45f;
  float wind_speed_ = 12.f;
  Vec2 wind_dir_{1.f, 0.f};
  float chop_ = 0.85f;
  float time_ = 0.f;
  Vec3 origin_{};  // world-space min corner of current tile

  // Spectrum H0 (complex interleaved re,im) size n*n*2
  std::vector<float> h0_;
  std::vector<float> spectrum_;  // working Ht
  terrain::Heightmap height_;
  std::vector<float> foam_;
  std::vector<float> disp_x_;
  std::vector<float> disp_z_;

  void BuildSpectrumSeed();
  void EvolveSpectrum();
  void Ifft2DHeight();
  void RebuildDerived();
  [[nodiscard]] float WrapLocal(float v) const;
};

// Convenience: build a water grid covering one ocean tile and animate from FFT.
terrain::TerrainMesh BuildOceanTileMesh(const FftOcean& ocean, int segments);

}  // namespace engine::ocean
