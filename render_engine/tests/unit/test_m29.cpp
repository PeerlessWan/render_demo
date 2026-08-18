#include "mini_test.h"

#include "engine/animation/blend_tree.h"
#include "engine/animation/skeleton.h"
#include "engine/animation/state_machine.h"
#include "engine/animation/two_bone_ik.h"
#include "engine/render/ies_profile.h"
#include "engine/render/local_lights.h"
#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

engine::animation::AnimationClip MakeTranslateClip(const char* name, engine::Vec3 end) {
  engine::animation::AnimationClip clip;
  clip.name = name;
  clip.duration = 1.f;
  clip.tracks.resize(1);
  clip.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  clip.tracks[0].push_back({1.f, engine::Quat::Identity(), end});
  return clip;
}

engine::animation::Skeleton MakeOneBoneSkel() {
  engine::animation::Skeleton skel;
  skel.joints.push_back({"root", -1, engine::Mat4::Identity()});
  return skel;
}

}  // namespace

TEST_CASE("SolveTwoBoneIK reaches in-range target", "[m29][w8][c11]") {
  engine::animation::TwoBoneIkInput in;
  in.root = {0, 0, 0};
  in.mid = {0, 1, 0};
  in.tip = {0, 2, 0};
  in.target = {1, 1, 0};
  in.pole = {0, 1, 1};
  const auto out = engine::animation::SolveTwoBoneIK(in);
  REQUIRE(out.reached);
  const float la = (out.mid - in.root).length();
  const float lb = (out.tip - out.mid).length();
  REQUIRE(std::fabs(la - 1.f) < 1e-3f);
  REQUIRE(std::fabs(lb - 1.f) < 1e-3f);
  REQUIRE((out.tip - in.target).length() < 1e-2f);
}

TEST_CASE("SolveTwoBoneIK clamps beyond reach", "[m29][w8][c11b]") {
  engine::animation::TwoBoneIkInput in;
  in.root = {0, 0, 0};
  in.mid = {0, 1, 0};
  in.tip = {0, 2, 0};
  in.target = {10, 0, 0};
  in.pole = {0, 1, 0};
  const auto out = engine::animation::SolveTwoBoneIK(in);
  REQUIRE_FALSE(out.reached);
  REQUIRE((out.tip - in.root).length() < 2.01f);
}

TEST_CASE("SampleBlend1D lerps neighbors", "[m29][w8][c10]") {
  const auto skel = MakeOneBoneSkel();
  engine::animation::BlendPoint1D pts[2] = {
      {0.f, MakeTranslateClip("a", {0, 2, 0})},
      {1.f, MakeTranslateClip("b", {2, 0, 0})},
  };
  const auto pose = engine::animation::SampleBlend1D(skel, pts, 0.5f, 1.f);
  REQUIRE(pose.bone_matrices.size() == 1);
  REQUIRE(std::fabs(pose.bone_matrices[0].m[12] - 1.f) < 0.05f);
  REQUIRE(std::fabs(pose.bone_matrices[0].m[13] - 1.f) < 0.05f);
}

TEST_CASE("SampleBlendSpace2D prefers nearest sample", "[m29][w8][c10b]") {
  const auto skel = MakeOneBoneSkel();
  engine::animation::BlendPoint2D pts[3] = {
      {0.f, 0.f, MakeTranslateClip("idle", {0, 0, 0})},
      {1.f, 0.f, MakeTranslateClip("right", {2, 0, 0})},
      {0.f, 1.f, MakeTranslateClip("up", {0, 2, 0})},
  };
  const auto pose = engine::animation::SampleBlendSpace2D(skel, pts, 1.f, 0.f, 1.f);
  REQUIRE(std::fabs(pose.bone_matrices[0].m[12] - 2.f) < 0.05f);
}

TEST_CASE("BlendPosesWithMask and SampleTree Masked", "[m29][w8][c10c]") {
  engine::animation::Skeleton skel;
  skel.joints.push_back({"root", -1, engine::Mat4::Identity()});
  skel.joints.push_back({"arm", 0, engine::Mat4::Identity()});

  engine::animation::AnimationClip base_clip;
  base_clip.duration = 1.f;
  base_clip.tracks.resize(2);
  base_clip.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  base_clip.tracks[1].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});

  engine::animation::AnimationClip overlay_clip;
  overlay_clip.duration = 1.f;
  overlay_clip.tracks.resize(2);
  overlay_clip.tracks[0].push_back({0.f, engine::Quat::Identity(), {1, 0, 0}});
  overlay_clip.tracks[1].push_back({0.f, engine::Quat::Identity(), {0, 3, 0}});

  engine::animation::BoneMask mask;
  mask.weights = {0.f, 1.f};

  engine::animation::BlendTreeNode tree;
  tree.op = engine::animation::BlendTreeOp::Masked;
  tree.mask = mask;
  tree.alpha = 1.f;
  tree.children.resize(2);
  tree.children[0].op = engine::animation::BlendTreeOp::Clip;
  tree.children[0].clip = base_clip;
  tree.children[1].op = engine::animation::BlendTreeOp::Clip;
  tree.children[1].clip = overlay_clip;

  const auto pose = engine::animation::SampleTree(skel, tree, 0.f);
  REQUIRE(pose.bone_matrices.size() == 2);
  // root masked 0 → keep base (identity translation)
  REQUIRE(std::fabs(pose.bone_matrices[0].m[12]) < 0.05f);
  // arm masked 1 → overlay
  REQUIRE(std::fabs(pose.bone_matrices[1].m[13] - 3.f) < 0.05f);
}

TEST_CASE("AnimNotify fires on Update crossing", "[m29][w8][c10d]") {
  engine::animation::AnimationClip clip;
  clip.name = "idle";
  clip.duration = 1.f;
  clip.tracks.resize(1);
  clip.tracks[0].push_back({0.f, engine::Quat::Identity(), {}});

  engine::animation::AnimationStateMachine sm;
  sm.AddState({"idle", clip, true});
  sm.AddNotify("idle", {"foot_l", 0.25f});
  sm.AddNotify("idle", {"foot_r", 0.75f});
  REQUIRE(sm.NotifiesFor("idle").size() == 2);

  sm.Update(0.2f);
  REQUIRE(sm.DrainNotifies().empty());
  sm.Update(0.1f);
  auto fired = sm.DrainNotifies();
  REQUIRE(fired.size() == 1);
  REQUIRE(fired[0].name == "foot_l");
}

TEST_CASE("LoadIesText builds LUT usable by EvalIesFactor", "[m29][w8][c03]") {
  // Minimal LM-63-like photometric block (1 horiz, 3 vert).
  const char* ies = R"(IESNA:LM-63-2002
TILT=NONE
1 -1 1 3 1 1 1 0 0 0
1 1 0
0 45 90
0
100 40 10
)";
  auto loaded = engine::render::LoadIesText(ies);
  REQUIRE(loaded);
  REQUIRE(loaded->samples.size() >= 2);
  const float on_axis = engine::render::EvalIesFactor(1.f, loaded.value());
  const float off_axis = engine::render::EvalIesFactor(0.f, loaded.value());
  REQUIRE(on_axis > off_axis);

  engine::render::ClearRegisteredIesLuts();
  const int id = engine::render::RegisterIesLut(loaded.value());
  REQUIRE(id >= 100);
  REQUIRE(std::fabs(engine::render::EvalIesFactor(1.f, id) - on_axis) < 1e-5f);
  engine::render::ClearRegisteredIesLuts();
}

TEST_CASE("Tile light pack constants", "[m29][w8][c02]") {
  static_assert(engine::render::kLightTileGridW == 8);
  static_assert(engine::render::kLightTileGridH == 4);
  static_assert(engine::render::kLightTileCount == 32);
  static_assert(engine::render::kLightZSlices == 4);
  static_assert(engine::render::kLightClusterCount == 128);
  static_assert(engine::render::kMaxLightsPerTile == 8);
  static_assert(engine::render::kTileLightIndexCount == 1024);
  REQUIRE(engine::render::kTileLightIndexCount ==
          engine::render::kLightClusterCount * engine::render::kMaxLightsPerTile);
}

TEST_CASE("PackTileLightLists populates FrameLighting-shaped arrays", "[m29][w8][c02]") {
  std::vector<engine::render::LocalLight> lights(3);
  lights[0].position = {-2.f, 0.f, -5.f};
  lights[0].range = 0.05f;
  lights[1].position = {2.f, 0.f, -5.f};
  lights[1].range = 0.05f;
  lights[2].position = {0.f, 0.f, -5.f};
  lights[2].range = 0.05f;
  const engine::Mat4 view = engine::Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::vector<std::vector<int>> tiles;
  engine::render::AssignLightsToTiles(lights, vp, engine::render::kLightTileGridW,
                                      engine::render::kLightTileGridH, tiles);
  REQUIRE(tiles.size() == static_cast<std::size_t>(engine::render::kLightClusterCount));

  std::array<int, engine::render::kLightClusterCount> counts{};
  std::array<int, engine::render::kTileLightIndexCount> indices{};
  engine::render::PackTileLightLists(tiles, counts, indices);

  int populated = 0;
  int total_slots = 0;
  for (int t = 0; t < engine::render::kLightClusterCount; ++t) {
    REQUIRE(counts[static_cast<std::size_t>(t)] >= 0);
    REQUIRE(counts[static_cast<std::size_t>(t)] <= engine::render::kMaxLightsPerTile);
    if (counts[static_cast<std::size_t>(t)] > 0) {
      ++populated;
      total_slots += counts[static_cast<std::size_t>(t)];
    }
  }
  REQUIRE(populated >= 2);
  REQUIRE(total_slots >= 3);

  engine::rhi::FrameLighting lighting{};
  lighting.enable_tiled_lights = true;
  lighting.local_light_count = 3;
  for (int t = 0; t < engine::render::kLightClusterCount; ++t) {
    lighting.tile_light_count[static_cast<std::size_t>(t)] = counts[static_cast<std::size_t>(t)];
  }
  for (int i = 0; i < engine::render::kTileLightIndexCount; ++i) {
    lighting.tile_light_index[static_cast<std::size_t>(i)] = indices[static_cast<std::size_t>(i)];
  }
  REQUIRE(lighting.enable_tiled_lights);
  REQUIRE(lighting.tile_light_count.size() == 128);
  REQUIRE(lighting.tile_light_index.size() == 1024);
}

TEST_CASE("EvalTiledLightList matches AssignLightsToTiles bins", "[m29][w8][c02]") {
  std::vector<engine::render::LocalLight> lights(2);
  lights[0].position = {-2.f, 0.f, -5.f};
  lights[1].position = {2.f, 0.f, -5.f};
  const engine::Mat4 view = engine::Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 2.f, 0.1f, 100.f);
  const engine::Mat4 vp = proj * view;

  std::vector<std::vector<int>> tiles;
  engine::render::AssignLightsToTiles(lights, vp, engine::render::kLightTileGridW,
                                      engine::render::kLightTileGridH, tiles);
  std::array<int, engine::render::kLightClusterCount> counts{};
  std::array<int, engine::render::kTileLightIndexCount> indices{};
  engine::render::PackTileLightLists(tiles, counts, indices);

  const engine::Vec3 ndc0 = vp.TransformPoint(lights[0].position);
  const float u0 = std::clamp(ndc0.x * 0.5f + 0.5f, 0.f, 0.999f);
  const float v0 = std::clamp(ndc0.y * 0.5f + 0.5f, 0.f, 0.999f);
  std::vector<int> eval0;
  engine::render::EvalTiledLightList(counts, indices, u0, v0, /*view_z=*/5.f, eval0);
  REQUIRE_FALSE(eval0.empty());
  bool found0 = false;
  for (int idx : eval0) {
    if (idx == 0) {
      found0 = true;
    }
  }
  REQUIRE(found0);

  const engine::Vec3 ndc1 = vp.TransformPoint(lights[1].position);
  const float u1 = std::clamp(ndc1.x * 0.5f + 0.5f, 0.f, 0.999f);
  const float v1 = std::clamp(ndc1.y * 0.5f + 0.5f, 0.f, 0.999f);
  std::vector<int> eval1;
  engine::render::EvalTiledLightList(counts, indices, u1, v1, /*view_z=*/5.f, eval1);
  REQUIRE_FALSE(eval1.empty());
  bool found1 = false;
  for (int idx : eval1) {
    if (idx == 1) {
      found1 = true;
    }
  }
  REQUIRE(found1);

  std::vector<int> edge;
  engine::render::EvalTiledLightList(counts, indices, -1.f, 2.f, 5.f, edge);
  REQUIRE(edge.size() <= static_cast<std::size_t>(engine::render::kMaxLightsPerTile));
}

TEST_CASE("EffectTuning enable_tiled_lights defaults on", "[m29][w8][c02]") {
  engine::render::EffectTuning fx;
  REQUIRE(fx.enable_tiled_lights);
}
