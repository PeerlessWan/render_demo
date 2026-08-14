#include "engine/core/feature.h"
#include "engine/core/log.h"
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

  const engine::FeatureSet features = engine::QueryFeatures();
  engine::rt::DxrDemoConfig demo;
  demo.enable_reflections = true;
  demo.enable_shadows = false;
  demo.max_bounces = 1;

  const bool can_run = engine::rt::CanRunDxrDemo(features, demo);
  engine::LogInfo(std::string("CanRunDxrDemo=") + (can_run ? "true" : "false") +
                  " raytracing=" + (features.raytracing ? "true" : "false") +
                  " d3d12=" + (features.d3d12 ? "true" : "false"));

  engine::rt::RaytracingConfig cfg;
  cfg.enable = can_run;
  cfg.allow_fallback = true;
  const auto rt = engine::rt::Resolve(engine::rhi::Backend::D3D12, features, cfg);
  engine::LogInfo("RtStatus=" + std::to_string(static_cast<int>(rt)));

  (void)headless_frames;
  return 0;
}
