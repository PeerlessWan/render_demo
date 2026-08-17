#pragma once

#include "engine/core/math.h"

#include <string>
#include <vector>

namespace engine::animation {

struct Joint {
  std::string name;
  int parent = -1;
  Mat4 inverse_bind = Mat4::Identity();
};

struct Skeleton {
  std::vector<Joint> joints;
};

struct SkinPose {
  std::vector<Mat4> bone_matrices;
};

struct ClipKey {
  float t = 0.f;
  Quat rotation = Quat::Identity();
  Vec3 translation{};
};

struct AnimationClip {
  std::string name;
  float duration = 1.f;
  std::vector<std::vector<ClipKey>> tracks;
};

SkinPose SampleClip(const Skeleton& skel, const AnimationClip& clip, float time);
Vec3 SkinVertexCpu(const Vec3& pos, const SkinPose& pose, const int bones[4],
                   const float weights[4]);

// W6/C12: GPU skin hot path is Feature-gated. Without a CS PSO this returns false and
// hosts should keep SkinVertexCpu / upload-skinned VB. Override: Feature "gpu_skinning".
[[nodiscard]] bool GpuSkinningAvailable();
// CPU stand-in for a future CS that writes a skinned vertex buffer (same contract).
void SkinVerticesGpuDispatchStub(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                                 const std::vector<int>& bones4, const std::vector<float>& weights4,
                                 std::vector<Vec3>& out_positions);

// M14: morph / blend-shape target (delta from bind pose).
struct MorphTarget {
  std::string name;
  std::vector<Vec3> deltas;  // per-vertex position deltas
};

// Apply weighted morph deltas onto bind positions. weights[i] multiplies targets[i].
// Output size matches bind_positions; missing deltas treated as zero.
void ApplyMorphTargets(const std::vector<Vec3>& bind_positions,
                       const std::vector<MorphTarget>& targets,
                       const std::vector<float>& weights,
                       std::vector<Vec3>& out_positions);

}  // namespace engine::animation
