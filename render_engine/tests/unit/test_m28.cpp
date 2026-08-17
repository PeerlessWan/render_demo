#include "mini_test.h"

#include "engine/animation/skeleton.h"
#include "engine/core/feature.h"
#include "engine/render/atmosphere.h"
#include "engine/render/ies_profile.h"
#include "engine/render2d/bmfont.h"
#include "engine/render2d/world_text.h"
#include "engine/rt/raytracing.h"

#include <cmath>

TEST_CASE("EvalCloudBand and CoupleFog finite", "[m28][w7][c05]") {
  engine::render::AtmosphereParams ap;
  ap.sun_dir = {0.2f, 1.f, 0.1f};
  const float c = engine::render::EvalCloudBand(ap, {0.f, 0.25f, 1.f});
  REQUIRE(c >= 0.f);
  REQUIRE(c <= 1.f);
  const auto fog = engine::render::CoupleFogWithAtmosphere(ap, {0.f, 0.2f, 1.f}, 0.02f, true);
  REQUIRE(std::isfinite(fog.fog_density));
  REQUIRE(fog.fog_density >= 0.02f);
  REQUIRE(std::isfinite(fog.fog_color.r));
}

TEST_CASE("EvalIesFactor profiles differ", "[m28][w7][c03]") {
  REQUIRE(std::fabs(engine::render::EvalIesFactor(1.f, 0) - 1.f) < 1e-5f);
}

TEST_CASE("EvalIesFactor narrow tighter than wide", "[m28][w7][c03b]") {
  const float mid = 0.5f;
  const float narrow = engine::render::EvalIesFactor(mid, 1);
  const float wide = engine::render::EvalIesFactor(mid, 2);
  REQUIRE(narrow < wide);
  REQUIRE(engine::render::SampleIesLut(0.f, 1) > engine::render::SampleIesLut(0.9f, 1));
}

TEST_CASE("BuildWorldTextBillboards emits quads", "[m28][w7][c14]") {
  engine::render2d::BmFontAtlas atlas;
  atlas.line_height = 16;
  atlas.glyphs['A'] = {0, 0, 8, 12, 9};
  atlas.glyphs['B'] = {8, 0, 8, 12, 9};
  const auto mesh = engine::render2d::BuildWorldTextBillboards(
      atlas, "AB", {0, 1, 0}, {1, 0, 0}, {0, 1, 0}, 0.05f);
  REQUIRE(mesh.vertices.size() == 8);
  REQUIRE(mesh.indices.size() == 12);
}

TEST_CASE("GpuSkinningAvailable and SkinVerticesGpuDispatch", "[m28][w7][c12]") {
  engine::ClearFeatureOverrides();
  REQUIRE_FALSE(engine::animation::GpuSkinningAvailable());
  engine::SetFeatureOverride("gpu_skinning", true);
  REQUIRE(engine::animation::GpuSkinningAvailable());
  engine::animation::SkinPose pose;
  pose.bone_matrices.push_back(engine::Mat4::Translation({0, 1, 0}));
  std::vector<engine::Vec3> bind{{0, 0, 0}};
  std::vector<int> bones{0, 0, 0, 0};
  std::vector<float> weights{1.f, 0, 0, 0};
  std::vector<engine::Vec3> out;
  engine::animation::SkinVerticesGpuDispatch(bind, pose, bones, weights, out);
  REQUIRE(out.size() == 1);
  REQUIRE(std::fabs(out[0].y - 1.f) < 0.05f);
  engine::ClearFeatureOverrides();
}

TEST_CASE("TryBuildCubeBlasTlasAndDispatchRays soft path", "[m28][w7][dxr]") {
  const bool hw = engine::rt::ProbeDxrHardwareSupport();
  if (hw) {
    engine::SetFeatureOverride("raytracing", true);
    engine::SetFeatureOverride("d3d12", true);
  }
  const auto st = engine::rt::TryBuildCubeBlasTlasAndDispatchRays();
  if (!hw) {
    REQUIRE((!st && st.code() == engine::ErrorCode::Unavailable));
  } else {
    REQUIRE(st);
  }
  engine::ClearFeatureOverrides();
}
