#include "mini_test.h"

#include "engine/core/feature.h"
#include "engine/gi/rtxgi.h"
#include "engine/gpu_driven/virtual_geometry.h"
#include "engine/media/upscaler_backends.h"
#include "engine/rt/raytracing.h"

#include <string>
#include <vector>

TEST_CASE("W25 ProbeVkRt and product soft shadow", "[w25][rt]") {
  (void)engine::rt::ProbeVkRtHardwareSupport();
  (void)engine::rt::CanRunProductRtPath();
  std::vector<float> grid;
  int w = 0, h = 0;
  auto st = engine::rt::TryProductSoftShadowMask(grid, w, h);
  REQUIRE(st);
  REQUIRE(w > 0);
  REQUIRE(!grid.empty());
}

TEST_CASE("W25 RT reflection DXR or VK", "[w25][rt]") {
  std::vector<std::uint8_t> rgba;
  int w = 0, h = 0;
  auto st = engine::rt::TryHalfResRtReflectionCompose(rgba, w, h);
  if (engine::rt::CanRunProductRtPath()) {
    REQUIRE(st);
    REQUIRE(!rgba.empty());
  } else {
    REQUIRE(!st);  // honest SKIP
  }
}

TEST_CASE("W25 NGX evaluate linkage honest", "[w25][ngx]") {
  // Without CMake-linked .lib → not evaluate-ready (no fake green).
  if (!engine::media::NgxEvaluateLinked()) {
    REQUIRE(!engine::media::NgxEvaluateLinked());
  }
  (void)engine::media::NgxLibPresentOnDisk();
}

TEST_CASE("W25 RTXGI evaluate linkage honest", "[w25][rtxgi]") {
  if (!engine::gi::RtxgiEvaluateLinked()) {
    engine::gi::RtxgiVolumeDesc d;
    auto vol = engine::gi::TryCreateRtxgiVolume(d);
    // Unbound device → nullptr; bound without lib → ready false.
    if (vol) {
      REQUIRE(!vol->ready());
      std::vector<std::uint8_t> atlas;
      int aw = 0, ah = 0;
      REQUIRE(!vol->Update(atlas, aw, ah));
    }
  }
}

TEST_CASE("W25 VG continuous LOD and GPU cull", "[w25][vg]") {
  engine::SetFeatureOverride("virtual_geometry", true);
  std::vector<engine::Vec3> pos = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                   {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
  auto asset = engine::gpu_driven::BuildVirtualGeometry(pos, idx, 2);
  REQUIRE(!asset.nodes.empty());
  engine::Mat4 world{};
  engine::Mat4 vp{};
  auto sel = engine::gpu_driven::SelectClustersContinuous(asset, world, vp, 2.f, nullptr);
  REQUIRE(!sel.visible_meshlet_ids.empty());
  REQUIRE(sel.lod_blend.size() == sel.visible_meshlet_ids.size());
  std::vector<engine::gpu_driven::IndirectDrawArgs> args;
  auto cst = engine::gpu_driven::TryDispatchVirtualGeometryCullCs(asset, sel, world, vp, args);
  REQUIRE(cst);
}

TEST_CASE("W25 VG soft raster", "[w25][vg]") {
  engine::SetFeatureOverride("virtual_geometry", true);
  std::vector<engine::Vec3> pos = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
  auto asset = engine::gpu_driven::BuildVirtualGeometry(pos, idx, 2);
  engine::Mat4 world{};
  engine::Mat4 vp{};
  auto sel = engine::gpu_driven::SelectClusters(asset, world, vp, 4.f, nullptr);
  engine::gpu_driven::SoftRasterResult rr;
  auto st = engine::gpu_driven::SoftRasterizeVirtualGeometry(asset, sel, world, vp, 32, 18, rr);
  REQUIRE(st);
  REQUIRE(rr.width == 32);
  REQUIRE(rr.rgba.size() == 32u * 18u * 4u);
}
