#include "mini_test.h"

#include "engine/animation/gpu_skin_vk.h"
#include "engine/animation/skeleton.h"
#include "engine/assets/asset_hot_reload.h"
#include "engine/core/feature.h"
#include "engine/render/render_system.h"
#include "engine/render2d/atlas.h"
#include "engine/render2d/bmfont.h"
#include "engine/render2d/nineslice.h"
#include "engine/render2d/path2d.h"
#include "engine/render2d/rich_text.h"
#include "engine/render2d/world_text.h"
#include "engine/rhi/i_device.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("Path2D polyline and quadratic stroke mesh", "[m31][w8][g13]") {
  engine::render2d::Path2D path;
  path.MoveTo({0.f, 0.f});
  path.LineTo({10.f, 0.f});
  path.QuadraticTo({15.f, 10.f}, {20.f, 0.f}, 4);
  REQUIRE(path.points().size() >= 3);

  const auto lines = path.BuildLineList();
  REQUIRE(lines.topology == engine::render2d::Path2DTopology::LineList);
  REQUIRE(lines.vertices.size() >= 4);
  REQUIRE(lines.indices.size() >= 4);
  REQUIRE((lines.indices.size() % 2) == 0);

  const auto strip = path.BuildStrokeTriangleStrip(1.5f);
  REQUIRE(strip.topology == engine::render2d::Path2DTopology::TriangleStrip);
  REQUIRE(strip.vertices.size() >= 4);
  REQUIRE(strip.indices.size() >= 6);
}

TEST_CASE("NineSlice ExpandNineSlice emits 9 quads", "[m31][w8][c13]") {
  engine::render2d::NineSliceDesc desc;
  desc.position = {0, 0};
  desc.size = {100, 80};
  desc.source_w = 32;
  desc.source_h = 32;
  desc.border_l = desc.border_r = desc.border_t = desc.border_b = 8.f;
  desc.u0 = 0.f;
  desc.v0 = 0.f;
  desc.u1 = 1.f;
  desc.v1 = 1.f;
  const auto mesh = engine::render2d::ExpandNineSlice(desc);
  REQUIRE(mesh.vertices.size() == 36);  // 9 quads * 4
  REQUIRE(mesh.indices.size() == 54);   // 9 * 6
  float umin = 1.f;
  float umax = 0.f;
  for (const auto& v : mesh.vertices) {
    umin = std::min(umin, v.u);
    umax = std::max(umax, v.u);
  }
  REQUIRE(std::fabs(umin) < 1e-5f);
  REQUIRE(std::fabs(umax - 1.f) < 1e-5f);
}

TEST_CASE("ParseRichTextSpans color and newline", "[m31][w8][c15]") {
  const auto spans = engine::render2d::ParseRichTextSpans("Hi <color=#ff0000>Red</color><n/>Next");
  REQUIRE(spans.size() >= 3);
  REQUIRE(spans[0].text == "Hi ");
  bool found_red = false;
  bool found_nl = false;
  for (const auto& s : spans) {
    if (s.text == "Red") {
      found_red = true;
      REQUIRE(s.color.r > 0.9f);
      REQUIRE(s.color.g < 0.1f);
    }
    if (s.text == "\n") {
      found_nl = true;
    }
  }
  REQUIRE(found_red);
  REQUIRE(found_nl);
}

TEST_CASE("WorldText pairs with atlas frame and spans", "[m31][w8][c14]") {
  engine::render2d::BmFontAtlas atlas;
  atlas.pages.push_back("font.png");
  atlas.line_height = 16;
  atlas.glyphs['A'] = {0, 0, 8, 12, 9};
  atlas.glyphs['B'] = {8, 0, 8, 12, 9};

  engine::render2d::AtlasFrame frame;
  frame.name = "font.png";
  frame.u0 = 0.25f;
  frame.v0 = 0.25f;
  frame.u1 = 0.75f;
  frame.v1 = 0.75f;

  const auto mesh = engine::render2d::BuildWorldTextBillboardsWithAtlasFrame(
      atlas, frame, "AB", {0, 1, 0}, {1, 0, 0}, {0, 1, 0}, 0.05f);
  REQUIRE(mesh.vertices.size() == 8);
  REQUIRE(mesh.atlas_page == "font.png");
  REQUIRE(mesh.vertices[0].u >= 0.25f - 1e-4f);
  REQUIRE(mesh.vertices[0].u <= 0.75f + 1e-4f);

  const auto spans = engine::render2d::ParseRichTextSpans("<c=#00ff00>A</c>B");
  const auto colored = engine::render2d::BuildWorldTextBillboardsSpans(
      atlas, spans, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0.05f);
  REQUIRE(colored.vertices.size() == 8);
  REQUIRE(colored.vertices[0].color.g > 0.9f);
}

TEST_CASE("AssetHotReload Poll detects texture change", "[m31][w8][c16]") {
  const auto dir = std::filesystem::temp_directory_path() / "engine_m31_asset_hot";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "dummy.png");
    out << "png1";
  }
  engine::assets::AssetHotReload hot;
  hot.SetRoot(dir);
  REQUIRE_FALSE(hot.Poll());
  REQUIRE_FALSE(hot.NeedsInvalidate());
  {
    std::ofstream out(dir / "extra.dds");
    out << "dds";
  }
  REQUIRE(hot.Poll());
  REQUIRE(hot.NeedsInvalidate());
  REQUIRE(hot.ConsumeInvalidateRequest());
  REQUIRE_FALSE(hot.NeedsInvalidate());
  std::filesystem::remove_all(dir, ec);
}

TEST_CASE("EffectTuning lens dirt flare default zero", "[m31][w8][c04]") {
  engine::render::EffectTuning fx;
  REQUIRE(std::fabs(fx.lens_distortion) < 1e-6f);
  REQUIRE(std::fabs(fx.light_dirt_strength) < 1e-6f);
  REQUIRE(std::fabs(fx.flare_strength) < 1e-6f);
  engine::rhi::PostResolveDesc post;
  REQUIRE(std::fabs(post.lens_distortion) < 1e-6f);
  post.enable_tonemap = false;
  post.enable_auto_exposure = false;
  post.exposure = 1.f;
  REQUIRE_FALSE(post.NeedsResolve());
  post.lens_distortion = 0.2f;
  REQUIRE(post.NeedsResolve());
  post.lens_distortion = 0.f;
  post.flare_strength = 0.3f;
  REQUIRE(post.NeedsResolve());
}

TEST_CASE("TryDispatchGpuSkinVk Feature gated SKIP or soft path", "[m31][w8][c12]") {
  engine::ClearFeatureOverrides();
  engine::animation::SkinPose pose;
  pose.bone_matrices.push_back(engine::Mat4::Translation({0, 1, 0}));
  std::vector<engine::Vec3> bind{{0, 0, 0}};
  std::vector<int> bones{0, 0, 0, 0};
  std::vector<float> weights{1.f, 0, 0, 0};
  std::vector<engine::Vec3> out;

  auto st = engine::animation::DispatchGpuSkinVkStatus(bind, pose, bones, weights, out, {});
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);

  engine::SetFeatureOverride("gpu_skinning", true);
  st = engine::animation::DispatchGpuSkinVkStatus(bind, pose, bones, weights, out, {});
#if ENGINE_WITH_VULKAN
  // May succeed if SPIR-V present, else Unavailable — never hard-fail the suite.
  if (st) {
    REQUIRE(out.size() == 1);
    REQUIRE(std::fabs(out[0].y - 1.f) < 0.05f);
  } else {
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  }
#else
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
#endif
  engine::ClearFeatureOverrides();
}
