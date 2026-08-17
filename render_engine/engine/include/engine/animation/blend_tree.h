#pragma once

#include "engine/animation/skeleton.h"

#include <span>
#include <string>
#include <vector>

namespace engine::animation {

// C10 / Mega-W8: thin blend tree + 1D/2D blend spaces + bone mask.

struct BlendPoint1D {
  float x = 0.f;
  AnimationClip clip;
};

struct BlendPoint2D {
  float x = 0.f;
  float y = 0.f;
  AnimationClip clip;
};

struct BoneMask {
  // Per-joint weights in [0,1]. Missing / short → treated as 0 (keep base).
  std::vector<float> weights;
};

// Linear blend of clip samples along a 1D parameter (weights from neighbor distances).
[[nodiscard]] SkinPose SampleBlend1D(const Skeleton& skel, std::span<const BlendPoint1D> points,
                                     float x, float time);

// Inverse-distance weighted blend of 2D blend-space samples.
[[nodiscard]] SkinPose SampleBlendSpace2D(const Skeleton& skel,
                                          std::span<const BlendPoint2D> points, float x, float y,
                                          float time);

// out[i] = lerp(base[i], overlay[i], mask[i] * alpha).
[[nodiscard]] SkinPose BlendPosesWithMask(const SkinPose& base, const SkinPose& overlay,
                                          const BoneMask& mask, float alpha = 1.f);

enum class BlendTreeOp {
  Clip = 0,
  Blend1D,
  Blend2D,
  Masked,  // child_a = base, child_b = overlay
};

struct BlendTreeNode {
  BlendTreeOp op = BlendTreeOp::Clip;
  AnimationClip clip;
  std::vector<BlendPoint1D> points1d;
  std::vector<BlendPoint2D> points2d;
  float param_x = 0.f;
  float param_y = 0.f;
  BoneMask mask;
  float alpha = 1.f;
  // Owned children for Masked (and optional nesting).
  std::vector<BlendTreeNode> children;
};

[[nodiscard]] SkinPose SampleTree(const Skeleton& skel, const BlendTreeNode& node, float time);

}  // namespace engine::animation
