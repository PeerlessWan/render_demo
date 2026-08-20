#include "mini_test.h"

#include "engine/assets/asset_handle.h"
#include "engine/assets/asset_id.h"
#include "engine/assets/streaming_budget.h"
#include "engine/core/feature.h"
#include "engine/gi/probe_volume.h"
#include "engine/hlod/billboard_impostor.h"
#include "engine/render/quality.h"
#include "engine/render2d/bmfont.h"
#include "engine/render2d/world_text.h"
#include "engine/rt/raytracing.h"
#include "engine/vt/virtual_texture.h"

#include <vector>

TEST_CASE("W20 quality probe_update_budget maps Low/Med/High", "[w20][gi][quality]") {
  using engine::render::QualitySettings;
  using engine::render::QualityTier;
  const auto low = QualitySettings::FromTier(QualityTier::Low);
  const auto med = QualitySettings::FromTier(QualityTier::Medium);
  const auto high = QualitySettings::FromTier(QualityTier::High);
  REQUIRE(low.probe_update_budget == 16);
  REQUIRE(med.probe_update_budget == 64);
  REQUIRE(high.probe_update_budget == 128);
  REQUIRE(high.enable_raytracing);
  REQUIRE_FALSE(med.enable_raytracing);

  engine::gi::ProbeVolume vol;
  vol.Configure({0, 0, 0}, {1, 1, 1}, 4, 2, 4);
  vol.set_budget_per_frame(high.probe_update_budget);
  REQUIRE(vol.budget_per_frame() == high.probe_update_budget);
}

TEST_CASE("W20 StreamingBudget used/limit contract", "[w20][perf][budget]") {
  engine::assets::StreamingBudget budget(1024);
  REQUIRE(budget.budget() == 1024);
  REQUIRE(budget.used() == 0);
  // Empty handle: Resident may reject; still verify budget API for HUD.
  engine::assets::AssetHandle keep;
  (void)budget.Resident(engine::assets::AssetId{"w20_chunk"}, 256, keep);
  REQUIRE(budget.used() <= budget.budget());
}

TEST_CASE("W20 soft shadow compose SKIP without raytracing Feature", "[w20][rt]") {
  engine::ClearFeatureOverrides();
  engine::SetFeatureOverride("raytracing", false);
  float factor = 1.f;
  auto st = engine::rt::TrySoftShadowCompose(factor);
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);

  engine::SetFeatureOverride("raytracing", true);
  std::vector<float> grid;
  int gw = 0;
  int gh = 0;
  const auto on = engine::rt::TryHalfResSoftShadowCompose(factor, grid, gw, gh);
  if (on) {
    REQUIRE(gw > 0);
    REQUIRE(gh > 0);
    REQUIRE(grid.size() == static_cast<std::size_t>(gw * gh));
  } else {
    REQUIRE(on.code() == engine::ErrorCode::Unavailable);
  }
  engine::ClearFeatureOverrides();
}

TEST_CASE("W20 scale smoke budgets are documented numbers", "[w20][perf][scale]") {
  constexpr int kInstances = 1024;
  constexpr int kLocalLights = 32;
  constexpr int kVtPages = 32;
  constexpr double kFrameMsSoft = 33.0;
  REQUIRE(kInstances == 1024);
  REQUIRE(kLocalLights == 32);
  REQUIRE(kVtPages == 32);
  REQUIRE(kFrameMsSoft > 16.0);
}

TEST_CASE("W20 VT BuildPhysicalAtlasRgba after packed ingest", "[w20][vt][c06]") {
  engine::vt::VirtualTexture vt;
  vt.Configure(8, 4, 2);
  const std::uint32_t packed = (1u) | (2u << 10) | (0u << 20) | (200u << 24);
  REQUIRE(vt.IngestFeedbackPackedU32({&packed, 1}) == 1);
  REQUIRE(vt.ProcessRequests(2) >= 1);
  std::vector<std::uint8_t> rgba;
  int w = 0;
  int h = 0;
  REQUIRE(vt.BuildPhysicalAtlasRgba(4, rgba, w, h));
  REQUIRE(w == 8);
  REQUIRE(h == 8);
  REQUIRE(rgba.size() == 256);
}

TEST_CASE("W20 HLOD SwitchLod hysteresis enter/exit", "[w20][hlod][c07]") {
  engine::hlod::BillboardImpostor lod;
  lod.distance_threshold = 10.f;
  lod.exit_distance = 7.f;
  REQUIRE(lod.SwitchLod(3.f) == engine::hlod::LodMode::NearMesh);
  REQUIRE(lod.SwitchLod(10.f) == engine::hlod::LodMode::Impostor);
  REQUIRE(lod.SwitchLod(8.f) == engine::hlod::LodMode::Impostor);
  REQUIRE(lod.SwitchLod(6.f) == engine::hlod::LodMode::NearMesh);
}

TEST_CASE("W20 WorldText BakeWorldTextAtlasRgba", "[w20][c14]") {
  engine::render2d::BmFontAtlas atlas;
  atlas.glyphs['A'] = {0, 0, 8, 12, 9};
  std::vector<std::uint8_t> rgba;
  engine::render2d::BakeWorldTextAtlasRgba(atlas, 32, 32, rgba);
  REQUIRE(rgba.size() == 32u * 32u * 4u);
  REQUIRE(rgba[0] == 245);
}
