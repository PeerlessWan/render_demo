#include "engine/animation/skeleton.h"
#include "engine/core/log.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

void ParseHeadless(int argc, char** argv, int& headless_frames) {
  bool headless = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      headless = true;
      if (headless_frames <= 0) {
        headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      headless_frames = std::atoi(argv[++i]);
    }
  }
  if (headless && headless_frames <= 0) {
    headless_frames = 2;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int headless_frames = 0;
  ParseHeadless(argc, argv, headless_frames);

  engine::animation::Skeleton skel;
  skel.joints.push_back({"root", -1, engine::Mat4::Identity()});
  skel.joints.push_back({"bone1", 0, engine::Mat4::Identity()});

  engine::animation::AnimationClip clip;
  clip.name = "wave";
  clip.duration = 1.f;
  clip.tracks.resize(2);
  clip.tracks[0].push_back({0.f, engine::Quat::Identity(), {}});
  clip.tracks[1].push_back({0.f, engine::Quat::Identity(), {0.f, 0.f, 0.f}});
  clip.tracks[1].push_back(
      {0.5f, engine::Quat::FromEulerYxz(0.4f, 0.f, 0.f), {0.f, 0.2f, 0.f}});
  clip.tracks[1].push_back({1.f, engine::Quat::Identity(), {0.f, 0.f, 0.f}});

  const auto pose = engine::animation::SampleClip(skel, clip, 0.5f);
  engine::LogInfo("SkinPose bones=" + std::to_string(pose.bone_matrices.size()));

  const int bones[4] = {0, 1, 0, 0};
  const float weights[4] = {0.5f, 0.5f, 0.f, 0.f};
  const auto skinned =
      engine::animation::SkinVertexCpu({0.f, 1.f, 0.f}, pose, bones, weights);
  engine::LogInfo("SkinVertexCpu y=" + std::to_string(skinned.y));

  std::vector<engine::Vec3> bind{{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
  std::vector<int> bones4{0, 1, 0, 0, 0, 1, 0, 0};
  std::vector<float> w4{1.f, 0.f, 0.f, 0.f, 0.5f, 0.5f, 0.f, 0.f};
  std::vector<engine::Vec3> out;
  engine::animation::SkinVerticesGpuDispatchStub(bind, pose, bones4, w4, out);
  engine::LogInfo("GpuDispatchStub verts=" + std::to_string(out.size()) +
                  " gpu_skinning_available=" +
                  (engine::animation::GpuSkinningAvailable() ? "true" : "false"));

  (void)headless_frames;
  return 0;
}
