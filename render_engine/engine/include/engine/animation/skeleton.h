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

}  // namespace engine::animation
