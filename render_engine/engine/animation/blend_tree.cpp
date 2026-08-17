#include "engine/animation/blend_tree.h"

#include <algorithm>
#include <cmath>

namespace engine::animation {
namespace {

void AccumulatePose(SkinPose& acc, const SkinPose& pose, float w) {
  if (acc.bone_matrices.size() < pose.bone_matrices.size()) {
    const std::size_t old = acc.bone_matrices.size();
    acc.bone_matrices.resize(pose.bone_matrices.size(), Mat4::Identity());
    for (std::size_t i = old; i < acc.bone_matrices.size(); ++i) {
      acc.bone_matrices[i].m.fill(0.f);
    }
  }
  for (std::size_t i = 0; i < pose.bone_matrices.size(); ++i) {
    for (int k = 0; k < 16; ++k) {
      acc.bone_matrices[i].m[k] += pose.bone_matrices[i].m[k] * w;
    }
  }
}

void ScalePose(SkinPose& pose, float inv) {
  for (auto& m : pose.bone_matrices) {
    for (int k = 0; k < 16; ++k) {
      m.m[k] *= inv;
    }
  }
}

SkinPose EmptyPose(const Skeleton& skel) {
  SkinPose p;
  p.bone_matrices.assign(skel.joints.size(), Mat4::Identity());
  return p;
}

SkinPose ZeroAcc(const Skeleton& skel) {
  SkinPose p;
  p.bone_matrices.assign(skel.joints.size(), Mat4::Identity());
  for (auto& m : p.bone_matrices) {
    m.m.fill(0.f);
  }
  return p;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

SkinPose SampleBlend1D(const Skeleton& skel, std::span<const BlendPoint1D> points, float x,
                       float time) {
  if (points.empty()) {
    return EmptyPose(skel);
  }
  if (points.size() == 1) {
    return SampleClip(skel, points[0].clip, time);
  }

  std::vector<std::size_t> order(points.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return points[a].x < points[b].x; });

  if (x <= points[order.front()].x) {
    return SampleClip(skel, points[order.front()].clip, time);
  }
  if (x >= points[order.back()].x) {
    return SampleClip(skel, points[order.back()].clip, time);
  }

  std::size_t i1 = 0;
  for (std::size_t i = 0; i + 1 < order.size(); ++i) {
    if (x >= points[order[i]].x && x <= points[order[i + 1]].x) {
      i1 = i;
      break;
    }
  }
  const auto& a = points[order[i1]];
  const auto& b = points[order[i1 + 1]];
  const float span = b.x - a.x;
  const float t = span > 1e-6f ? (x - a.x) / span : 0.f;
  SkinPose pa = SampleClip(skel, a.clip, time);
  SkinPose pb = SampleClip(skel, b.clip, time);
  SkinPose out = ZeroAcc(skel);
  AccumulatePose(out, pa, 1.f - t);
  AccumulatePose(out, pb, t);
  return out;
}

SkinPose SampleBlendSpace2D(const Skeleton& skel, std::span<const BlendPoint2D> points, float x,
                            float y, float time) {
  if (points.empty()) {
    return EmptyPose(skel);
  }
  if (points.size() == 1) {
    return SampleClip(skel, points[0].clip, time);
  }

  SkinPose out = ZeroAcc(skel);
  float wsum = 0.f;
  for (const auto& p : points) {
    const float dx = p.x - x;
    const float dy = p.y - y;
    const float d2 = dx * dx + dy * dy;
    if (d2 < 1e-10f) {
      return SampleClip(skel, p.clip, time);
    }
    const float w = 1.f / d2;
    AccumulatePose(out, SampleClip(skel, p.clip, time), w);
    wsum += w;
  }
  if (wsum <= 1e-8f) {
    return EmptyPose(skel);
  }
  ScalePose(out, 1.f / wsum);
  return out;
}

SkinPose BlendPosesWithMask(const SkinPose& base, const SkinPose& overlay, const BoneMask& mask,
                            float alpha) {
  SkinPose out = base;
  const std::size_t n =
      std::min({base.bone_matrices.size(), overlay.bone_matrices.size(), mask.weights.size()});
  if (out.bone_matrices.size() < overlay.bone_matrices.size()) {
    out.bone_matrices.resize(overlay.bone_matrices.size(), Mat4::Identity());
  }
  const float a = std::clamp(alpha, 0.f, 1.f);
  for (std::size_t i = 0; i < n; ++i) {
    const float w = std::clamp(mask.weights[i], 0.f, 1.f) * a;
    if (w <= 0.f) {
      continue;
    }
    if (w >= 1.f) {
      out.bone_matrices[i] = overlay.bone_matrices[i];
      continue;
    }
    for (int k = 0; k < 16; ++k) {
      out.bone_matrices[i].m[k] =
          Lerp(base.bone_matrices[i].m[k], overlay.bone_matrices[i].m[k], w);
    }
  }
  return out;
}

SkinPose SampleTree(const Skeleton& skel, const BlendTreeNode& node, float time) {
  switch (node.op) {
    case BlendTreeOp::Clip:
      return SampleClip(skel, node.clip, time);
    case BlendTreeOp::Blend1D:
      return SampleBlend1D(skel, node.points1d, node.param_x, time);
    case BlendTreeOp::Blend2D:
      return SampleBlendSpace2D(skel, node.points2d, node.param_x, node.param_y, time);
    case BlendTreeOp::Masked: {
      if (node.children.size() < 2) {
        return node.children.empty() ? EmptyPose(skel)
                                     : SampleTree(skel, node.children[0], time);
      }
      const SkinPose base = SampleTree(skel, node.children[0], time);
      const SkinPose overlay = SampleTree(skel, node.children[1], time);
      return BlendPosesWithMask(base, overlay, node.mask, node.alpha);
    }
    default:
      return EmptyPose(skel);
  }
}

}  // namespace engine::animation
