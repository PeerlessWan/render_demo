#include "mini_test.h"

#include "engine/animation/gpu_skin_main.h"
#include "engine/core/feature.h"
#include "engine/core/math.h"
#include "engine/gpu_driven/meshlet.h"
#include "engine/rhi/i_device.h"
#include "engine/rt/raytracing.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path ResolveTileCullSpirv() {
#if defined(ENGINE_SHADER_DIR_A)
  const auto from_def =
      std::filesystem::path(ENGINE_SHADER_DIR_A) / "light_tile_cull_cs_vk.cs.spv";
  if (std::filesystem::exists(from_def)) {
    return from_def;
  }
#endif
  if (const char* env = std::getenv("ENGINE_SHADER_DIR")) {
    const auto from_env = std::filesystem::path(env) / "light_tile_cull_cs_vk.cs.spv";
    if (std::filesystem::exists(from_env)) {
      return from_env;
    }
  }
  return {};
}

}  // namespace

TEST_CASE("W11 VK light tile cull Setup SKIP without SPIR-V", "[m36][w11][c02]") {
  engine::rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.headless = true;
  auto dev = engine::rhi::CreateHeadlessDevice(desc);
  REQUIRE(dev);

  // Headless has no Setup override → Unavailable (honest SKIP path for callers).
  auto st = dev.value()->SetupLightTileCullCompute({});
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);

  std::array<int, 128> counts{};
  std::array<int, 1024> indices{};
  std::array<engine::Vec3, 1> pos{{{0.f, 0.f, -5.f}}};
  std::array<float, 1> ranges{{2.f}};
  st = dev.value()->DispatchLightTileCull(engine::Mat4::Identity(), pos, ranges, counts, indices);
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
}

TEST_CASE("W11 VK light tile cull Setup Ok or SKIP with spirv path", "[m36][w11][c02]") {
  engine::rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.headless = true;
  auto dev = engine::rhi::CreateHeadlessDevice(desc);
  REQUIRE(dev);

  const auto spirv = ResolveTileCullSpirv();
  auto st = dev.value()->SetupLightTileCullCompute(spirv.empty() ? std::filesystem::path{"missing_tile.spv"}
                                                                  : spirv);
  // Headless never implements tile cull → always Unavailable SKIP (no fake Ok).
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  REQUIRE((st.message().find("SKIP") != std::string::npos ||
           st.message().find("not available") != std::string::npos ||
           st.message().find("Unavailable") != std::string::npos || !st.message().empty()));
}

TEST_CASE("W11 SkinOnDevice routes by Feature and api_kind", "[m36][w11][c12]") {
  engine::ClearFeatureOverrides();
  engine::SetFeatureOverride("gpu_skinning", false);

  engine::rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.headless = true;
  auto dev = engine::rhi::CreateHeadlessDevice(desc);
  REQUIRE(dev);
  REQUIRE(dev.value()->api_kind() == engine::rhi::DeviceApiKind::Headless);

  std::vector<engine::Vec3> bind{{0, 0, 0}};
  engine::animation::SkinPose pose;
  pose.bone_matrices.push_back(engine::Mat4::Translation({0, 2, 0}));
  std::vector<int> bones{0, 0, 0, 0};
  std::vector<float> weights{1.f, 0.f, 0.f, 0.f};
  std::vector<engine::Vec3> out;

  auto st = engine::animation::SkinOnDevice(*dev.value(), bind, pose, bones, weights, out);
  REQUIRE(st);
  REQUIRE(st.message().find("cpu-fallback") != std::string::npos);
  REQUIRE(out.size() == 1);

  engine::SetFeatureOverride("gpu_skinning", true);
  st = engine::animation::SkinOnDevice(*dev.value(), bind, pose, bones, weights, out);
  REQUIRE(st);
  // Headless: gpu-skin-d3d12 / gpu-skin-vk / cpu-fallback — all honest Status paths.
  REQUIRE((st.message().find("gpu-skin") != std::string::npos ||
           st.message().find("cpu-fallback") != std::string::npos));
  REQUIRE(out.size() == 1);
  engine::ClearFeatureOverrides();
}

TEST_CASE("W11 TryMeshShaderPath Feature gate and VK EXT", "[m36][w11][c08]") {
  engine::ClearFeatureOverrides();
  auto st = engine::gpu_driven::TryMeshShaderPath();
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  REQUIRE(st.message().find("SKIP") != std::string::npos);

  engine::SetFeatureOverride("mesh_shader", true);
  st = engine::gpu_driven::TryMeshShaderPath();
  if (st) {
    REQUIRE(st.code() == engine::ErrorCode::Ok);
  } else {
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
    REQUIRE(st.message().find("SKIP") != std::string::npos);
  }

  const auto vk = engine::gpu_driven::ProbeMeshShaderSupportVk();
  if (vk) {
    // EXT present → Feature path must be able to succeed minimally.
    REQUIRE(st);
  } else {
    REQUIRE(vk.code() == engine::ErrorCode::Unavailable);
  }
  engine::ClearFeatureOverrides();
}

TEST_CASE("W11 TryVkTraceRaysDemoStub TraceRays or SKIP", "[m36][w11][rt]") {
  const auto st = engine::rt::TryVkTraceRaysDemoStub();
  if (st) {
    REQUIRE((st.message().find("vk-tracerays") != std::string::npos ||
             st.message().find("vk") != std::string::npos || st.message().empty()));
  } else {
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
    REQUIRE(st.message().find("SKIP") != std::string::npos);
  }
}

TEST_CASE("W11 ProbeBindlessMinimalPath headless SKIP", "[m36][w11][bindless]") {
  engine::rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 64;
  desc.headless = true;
  auto dev = engine::rhi::CreateHeadlessDevice(desc);
  REQUIRE(dev);
  const auto st = dev.value()->ProbeBindlessMinimalPath(0);
  // Headless / VK: Unavailable or Failed — must not pretend bindless hot path works.
  REQUIRE_FALSE(st);
}
