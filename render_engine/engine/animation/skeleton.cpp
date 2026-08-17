#include "engine/animation/skeleton.h"

#include "engine/animation/gpu_skin_d3d12.h"
#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace engine::animation {
namespace {

Quat Nlerp(Quat a, Quat b, float t) {
  const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (dot < 0) {
    b.x = -b.x;
    b.y = -b.y;
    b.z = -b.z;
    b.w = -b.w;
  }
  Quat r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
         a.w + (b.w - a.w) * t};
  const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
  if (len > 1e-8f) {
    r.x /= len;
    r.y /= len;
    r.z /= len;
    r.w /= len;
  }
  return r;
}

}  // namespace

SkinPose SampleClip(const Skeleton& skel, const AnimationClip& clip, float time) {
  SkinPose pose;
  pose.bone_matrices.assign(skel.joints.size(), Mat4::Identity());
  if (clip.duration <= 0.f || clip.tracks.empty()) {
    return pose;
  }
  float t = time;
  if (clip.duration > 0.f) {
    t = std::fmod(time, clip.duration);
    if (t < 0) {
      t += clip.duration;
    }
    // Include the end key when time lands exactly on a multiple of duration.
    if (t <= 1e-6f && time > 0.f) {
      t = clip.duration;
    }
  }
  std::vector<Mat4> locals(skel.joints.size(), Mat4::Identity());
  for (std::size_t i = 0; i < skel.joints.size() && i < clip.tracks.size(); ++i) {
    const auto& track = clip.tracks[i];
    if (track.empty()) {
      continue;
    }
    std::size_t i1 = 0;
    while (i1 + 1 < track.size() && track[i1 + 1].t < t) {
      ++i1;
    }
    const std::size_t i2 = std::min(i1 + 1, track.size() - 1);
    float u = 0.f;
    if (i1 != i2 && track[i2].t > track[i1].t) {
      u = (t - track[i1].t) / (track[i2].t - track[i1].t);
    }
    const Quat r = Nlerp(track[i1].rotation, track[i2].rotation, u);
    const Vec3 p =
        track[i1].translation + (track[i2].translation - track[i1].translation) * u;
    locals[i] = Mat4::TRS(p, r, Vec3{1, 1, 1});
  }
  std::vector<Mat4> globals(skel.joints.size(), Mat4::Identity());
  for (std::size_t i = 0; i < skel.joints.size(); ++i) {
    const int p = skel.joints[i].parent;
    globals[i] = (p >= 0 ? globals[static_cast<std::size_t>(p)] : Mat4::Identity()) * locals[i];
    pose.bone_matrices[i] = globals[i] * skel.joints[i].inverse_bind;
  }
  return pose;
}

Vec3 SkinVertexCpu(const Vec3& pos, const SkinPose& pose, const int bones[4],
                   const float weights[4]) {
  Vec3 out{};
  for (int i = 0; i < 4; ++i) {
    if (weights[i] <= 0.f) {
      continue;
    }
    const int b = bones[i];
    if (b < 0 || static_cast<std::size_t>(b) >= pose.bone_matrices.size()) {
      continue;
    }
    out = out + pose.bone_matrices[static_cast<std::size_t>(b)].TransformPoint(pos) * weights[i];
  }
  return out;
}

bool GpuSkinningAvailable() {
  // Feature override gates the hot path; D3D12 CS runs when skin_cs.cso + probe device succeed.
  return QueryFeature("gpu_skinning");
}

void SkinVerticesGpuDispatchStub(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                                 const std::vector<int>& bones4, const std::vector<float>& weights4,
                                 std::vector<Vec3>& out_positions) {
  // Same contract as skin_cs.hlsl / SkinVerticesGpuDispatch writing out_positions.
  out_positions.resize(bind_positions.size());
  for (std::size_t i = 0; i < bind_positions.size(); ++i) {
    int bones[4] = {0, 0, 0, 0};
    float weights[4] = {1.f, 0.f, 0.f, 0.f};
    if (bones4.size() >= (i + 1) * 4 && weights4.size() >= (i + 1) * 4) {
      for (int k = 0; k < 4; ++k) {
        bones[k] = bones4[i * 4 + static_cast<std::size_t>(k)];
        weights[k] = weights4[i * 4 + static_cast<std::size_t>(k)];
      }
    }
    out_positions[i] = SkinVertexCpu(bind_positions[i], pose, bones, weights);
  }
}

Status SkinVerticesComputeCpuReference(const std::vector<Vec3>& bind_positions,
                                       const SkinPose& pose, const std::vector<int>& bones4,
                                       const std::vector<float>& weights4,
                                       std::vector<Vec3>& out_positions) {
  SkinVerticesGpuDispatchStub(bind_positions, pose, bones4, weights4, out_positions);
  return Status::Ok();
}

void SkinVerticesGpuDispatch(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                             const std::vector<int>& bones4, const std::vector<float>& weights4,
                             std::vector<Vec3>& out_positions) {
  if (GpuSkinningAvailable()) {
    const Status st =
        DispatchGpuSkinD3d12Status(bind_positions, pose, bones4, weights4, out_positions, {});
    if (st) {
      return;
    }
    LogInfo(std::string("SkinVerticesGpuDispatch: D3D12 CS unavailable (") + st.message() +
            "); using CPU stub");
  }
  SkinVerticesGpuDispatchStub(bind_positions, pose, bones4, weights4, out_positions);
}

void ApplyMorphTargets(const std::vector<Vec3>& bind_positions,
                       const std::vector<MorphTarget>& targets,
                       const std::vector<float>& weights,
                       std::vector<Vec3>& out_positions) {
  out_positions = bind_positions;
  const std::size_t n = targets.size() < weights.size() ? targets.size() : weights.size();
  for (std::size_t t = 0; t < n; ++t) {
    const float w = weights[t];
    if (std::fabs(w) < 1e-8f) {
      continue;
    }
    const auto& deltas = targets[t].deltas;
    const std::size_t count =
        deltas.size() < out_positions.size() ? deltas.size() : out_positions.size();
    for (std::size_t i = 0; i < count; ++i) {
      out_positions[i] = out_positions[i] + deltas[i] * w;
    }
  }
}

}  // namespace engine::animation
