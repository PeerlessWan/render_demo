#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/rhi/i_device.h"

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

  engine::rhi::DeviceDesc ddesc;
  ddesc.headless = true;
  ddesc.width = 64;
  ddesc.height = 64;
  ddesc.enable_hdr_output = true;

  auto device = engine::rhi::CreateHeadlessDevice(ddesc);
  if (!device) {
    engine::LogInfo("SKIP sample_28_hdr_color_sandbox (CreateHeadlessDevice: " +
                    device.status().message() + ")");
    (void)headless_frames;
    return 0;
  }

  const auto features = engine::QueryFeatures();
  engine::LogInfo(std::string("HDR request enable_hdr_output=true feature.hdr_output=") +
                  (features.hdr_output ? "true" : "false") +
                  " (headless/offscreen often ignores display HDR10)");

  // Teaching note: tonemap / color management live in post stack (CH16); this sample
  // only exercises the DeviceDesc HDR capability bit and Feature reporting.
  if (!features.hdr_output) {
    engine::LogInfo("SKIP display HDR path (no live swapchain HDR); contract logged — see "
                    "DeviceDesc::enable_hdr_output and FeatureSet::hdr_output");
  } else {
    engine::LogInfo("hdr_output feature reported true");
  }

  (void)headless_frames;
  return 0;
}
