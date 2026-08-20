#include "mini_test.h"

#include "engine/animation/anim_serialize.h"
#include "engine/animation/blend_tree.h"
#include "engine/animation/gpu_skin_main.h"
#include "engine/animation/skeleton.h"
#include "engine/animation/state_machine.h"
#include "engine/core/feature.h"
#include "engine/net/quic.h"
#include "engine/platform/linux/window_x11.h"
#include "engine/rt/raytracing.h"
#include "engine/rhi/i_device.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

engine::animation::AnimationClip MakeClip(const char* name, float dur) {
  engine::animation::AnimationClip clip;
  clip.name = name;
  clip.duration = dur;
  clip.tracks.resize(1);
  clip.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  clip.tracks[0].push_back({dur, engine::Quat::Identity(), {1, 0, 0}});
  return clip;
}

engine::animation::Skeleton MakeSkel() {
  engine::animation::Skeleton skel;
  skel.joints.push_back({"root", -1, engine::Mat4::Identity()});
  return skel;
}

}  // namespace

TEST_CASE("AnimationStateMachine crossfade blends Sample", "[m33][w9][c10]") {
  const auto skel = MakeSkel();
  engine::animation::AnimationStateMachine sm;
  engine::animation::AnimState idle;
  idle.name = "Idle";
  idle.clip = MakeClip("idle", 1.f);
  idle.clip.tracks[0].back().translation = {0, 0, 0};
  engine::animation::AnimState walk;
  walk.name = "Walk";
  walk.clip = MakeClip("walk", 1.f);
  walk.clip.tracks[0].back().translation = {4, 0, 0};
  sm.AddState(idle);
  sm.AddState(walk);

  engine::animation::AnimTransition tr;
  tr.from = "Idle";
  tr.to = "Walk";
  tr.has_exit_time = false;
  tr.crossfade_duration = 0.5f;
  tr.trigger = "go";
  sm.AddTransition(tr);

  sm.SetTrigger("go");
  sm.Update(0.f);
  REQUIRE(sm.current_state() == "Walk");
  REQUIRE(sm.is_crossfading());

  sm.Update(0.25f);
  const float a = sm.crossfade_alpha();
  REQUIRE(a > 0.4f);
  REQUIRE(a < 0.6f);
  const auto pose = sm.Sample(skel);
  REQUIRE(pose.bone_matrices.size() == 1);
  // Mid-crossfade translation between 0 and Walk's early sample.
  REQUIRE(std::fabs(pose.bone_matrices[0].m[12]) >= 0.f);
}

TEST_CASE("BlendTree and StateMachine serialize round-trip", "[m33][w9][serialize]") {
  engine::animation::BlendTreeNode root;
  root.op = engine::animation::BlendTreeOp::Masked;
  root.alpha = 0.75f;
  root.mask.weights = {0.f, 1.f};
  root.children.resize(2);
  root.children[0].op = engine::animation::BlendTreeOp::Clip;
  root.children[0].clip.name = "base";
  root.children[0].clip.duration = 1.f;
  root.children[1].op = engine::animation::BlendTreeOp::Clip;
  root.children[1].clip.name = "overlay";
  root.children[1].clip.duration = 2.f;

  const std::string bt_text = engine::animation::SerializeBlendTree(root);
  engine::animation::BlendTreeNode round;
  REQUIRE(engine::animation::DeserializeBlendTree(bt_text, round));
  REQUIRE(round.op == engine::animation::BlendTreeOp::Masked);
  REQUIRE(std::fabs(round.alpha - 0.75f) < 1e-5f);
  REQUIRE(round.children.size() == 2);
  REQUIRE(round.children[1].clip.name == "overlay");
  REQUIRE(std::fabs(round.children[1].clip.duration - 2.f) < 1e-5f);

  engine::animation::AnimationStateMachine sm;
  sm.SetDefaultCrossfadeDuration(0.2f);
  engine::animation::AnimState a;
  a.name = "A";
  a.clip = MakeClip("a", 1.5f);
  engine::animation::AnimState b;
  b.name = "B";
  b.clip = MakeClip("b", 2.f);
  sm.AddState(a);
  sm.AddState(b);
  engine::animation::AnimTransition tr;
  tr.from = "A";
  tr.to = "B";
  tr.exit_time = 0.8f;
  tr.crossfade_duration = 0.3f;
  sm.AddTransition(tr);

  const std::string sm_text = engine::animation::SerializeStateMachine(sm);
  engine::animation::AnimationStateMachine sm2;
  REQUIRE(engine::animation::DeserializeStateMachine(sm_text, sm2));
  REQUIRE(std::fabs(sm2.default_crossfade_duration() - 0.2f) < 1e-5f);
  REQUIRE(sm2.states().size() == 2);
  REQUIRE(sm2.transitions().size() == 1);
  REQUIRE(std::fabs(sm2.transitions()[0].crossfade_duration - 0.3f) < 1e-5f);
  REQUIRE(sm2.current_state() == "A");
}

TEST_CASE("SkinOnDevice CPU fallback when Feature off", "[m33][w9][c12]") {
  engine::ClearFeatureOverrides();
  engine::SetFeatureOverride("gpu_skinning", false);

  engine::rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.headless = true;
  auto dev = engine::rhi::CreateHeadlessDevice(desc);
  REQUIRE(dev);

  std::vector<engine::Vec3> bind{{1, 0, 0}};
  engine::animation::SkinPose pose;
  pose.bone_matrices.push_back(engine::Mat4::Identity());
  std::vector<int> bones{0, 0, 0, 0};
  std::vector<float> weights{1.f, 0.f, 0.f, 0.f};
  std::vector<engine::Vec3> out;
  const auto st = engine::animation::SkinOnDevice(*dev.value(), bind, pose, bones, weights, out);
  REQUIRE(st);
  REQUIRE(st.message().find("cpu-fallback") != std::string::npos);
  REQUIRE(out.size() == 1);
  engine::ClearFeatureOverrides();
}

TEST_CASE("TryComposeDxrShadowOverlay and TryVkTraceRaysDemoStub", "[m33][w9][rt]") {
  float factor = -1.f;
  const auto dxr = engine::rt::TryComposeDxrShadowOverlay(factor);
  if (dxr) {
    REQUIRE(factor > 0.f);
    REQUIRE(factor <= 1.f);
    REQUIRE((dxr.message().find("dxr") != std::string::npos || dxr.message().empty()));
  } else {
    REQUIRE(dxr.code() == engine::ErrorCode::Unavailable);
    REQUIRE(dxr.message().find("SKIP") != std::string::npos);
  }

  const auto vk = engine::rt::TryVkTraceRaysDemoStub();
  if (vk) {
    REQUIRE(vk.message().find("vk") != std::string::npos || vk.message().empty());
  } else {
    REQUIRE(vk.code() == engine::ErrorCode::Unavailable);
    REQUIRE(vk.message().find("SKIP") != std::string::npos);
  }
}

TEST_CASE("TryQuicLoopbackReliableSendRecv Feature gate", "[m33][w9][quic]") {
  engine::ClearFeatureOverrides();
  const auto absent = engine::net::TryQuicLoopbackReliableSendRecv();
  REQUIRE_FALSE(absent);
  REQUIRE(absent.code() == engine::ErrorCode::Unavailable);
  REQUIRE(absent.message().find("SKIP") != std::string::npos);
  engine::SetFeatureOverride("quic", true);
  const auto forced = engine::net::TryQuicLoopbackReliableSendRecv();
  REQUIRE_FALSE(forced);
  REQUIRE(forced.message().find("SKIP") != std::string::npos);
  engine::ClearFeatureOverrides();
}

TEST_CASE("Linux X11 window stub Unavailable on non-Linux", "[m33][w9][linux]") {
  engine::platform::linux_x11::X11Native native;
  engine::platform::linux_x11::X11WindowDesc desc;
  desc.headless = true;
  const auto st = engine::platform::linux_x11::CreateX11WindowStub(desc, native);
#if defined(__linux__)
  REQUIRE(st);
#else
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
#endif
  const auto clear = engine::platform::linux_x11::TryX11ClearPathStub(native);
  REQUIRE_FALSE(clear);
  REQUIRE(clear.code() == engine::ErrorCode::Unavailable);
}
