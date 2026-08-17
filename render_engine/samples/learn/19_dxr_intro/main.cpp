#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/rhi/backend.h"
#include "engine/rt/raytracing.h"

#include <cstdlib>
#include <string>

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

  const bool hw_dxr = engine::rt::ProbeDxrHardwareSupport();
  engine::SetFeatureOverride("raytracing", hw_dxr);

  const engine::FeatureSet features = engine::QueryFeatures();
  engine::rt::DxrDemoConfig demo;
  demo.enable_reflections = true;
  demo.enable_shadows = true;
  demo.max_bounces = 1;

  const bool can_run = engine::rt::CanRunDxrDemo(features, demo);
  engine::LogInfo(std::string("ProbeDxrHardwareSupport=") + (hw_dxr ? "true" : "false") +
                  " CanRunDxrDemo=" + (can_run ? "true" : "false") +
                  " raytracing=" + (features.raytracing ? "true" : "false") +
                  " d3d12=" + (features.d3d12 ? "true" : "false"));

  engine::rt::RaytracingConfig cfg;
  cfg.enable = can_run;
  cfg.allow_fallback = true;
  const auto rt = engine::rt::Resolve(engine::rhi::Backend::D3D12, features, cfg);
  engine::LogInfo("RtStatus=" + std::to_string(static_cast<int>(rt)));

  const auto shadow = engine::rt::DxrShadowDemo(features, demo);
  engine::LogInfo(std::string("DxrShadowDemo.would_run=") + (shadow.would_run ? "true" : "false"));

  const auto tlas = engine::rt::TryEmptyTlasPrebuild();
  engine::LogInfo(std::string("TryEmptyTlasPrebuild=") +
                  (tlas ? "Ok" : ("Unavailable: " + tlas.message())));

  const auto as_rays = engine::rt::TryBuildCubeBlasTlasAndDispatchRays();
  engine::LogInfo(std::string("TryBuildCubeBlasTlasAndDispatchRays=") +
                  (as_rays ? "Ok"
                           : ((as_rays.code() == engine::ErrorCode::Unavailable ? "Unavailable: "
                                                                               : "Failed: ") +
                              as_rays.message())));

  engine::rhi::DeviceDesc ddesc;
  ddesc.headless = true;
  ddesc.width = 64;
  ddesc.height = 64;
  auto device = engine::rhi::CreateHeadlessDevice(ddesc);
  if (!device) {
    engine::LogError("CreateHeadlessDevice failed: " + device.status().message());
    return 1;
  }
  const auto stub = engine::rt::RunDxrFullscreenStub(*device.value());
  engine::LogInfo(std::string("RunDxrFullscreenStub=") +
                  (stub ? "Ok" : (stub.code() == engine::ErrorCode::Unavailable
                                      ? ("Unavailable: " + stub.message())
                                      : stub.message())));

  if (!can_run) {
    engine::LogInfo("SKIP sample_19_dxr_intro (DXR unavailable or demo gates closed; "
                    "W7 AS/DispatchRays path exercised when HW present — see ADR 0030)");
    (void)headless_frames;
    return 0;
  }

  engine::LogInfo("DXR capable: Prefer TryBuildCubeBlasTlasAndDispatchRays (BLAS+TLAS+DispatchRays); "
                  "fallback empty-TLAS stub when PSO/lib missing (ADR 0030 W7)");
  (void)headless_frames;
  return 0;
}
