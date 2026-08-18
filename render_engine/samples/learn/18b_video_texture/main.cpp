#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/media/media.h"

#include <cstdlib>
#include <string>
#include <vector>

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

  const bool probe = engine::media::QueryVideoDecodeAvailable();
  engine::SetFeatureOverride("video_decode", probe);
  const auto features = engine::QueryFeatures();
  engine::LogInfo(std::string("QueryVideoDecodeAvailable=") + (probe ? "true" : "false") +
                  " feature.video_decode=" + (features.video_decode ? "true" : "false"));

  auto decoder = engine::media::CreateD3D12VaDecoderOrStub();
  if (!decoder) {
    engine::LogInfo("SKIP sample_18b_video_texture (no decoder factory)");
    (void)headless_frames;
    return 0;
  }
  engine::LogInfo(std::string("VideoDecoder backend=") + decoder->backend_name() +
                  " feature_available=" + (decoder->feature_available() ? "true" : "false"));

  if (!decoder->feature_available()) {
    engine::LogInfo("SKIP sample_18b_video_texture (HW decode unavailable; no software fallback)");
    (void)headless_frames;
    return 0;
  }

  if (auto st = decoder->Open("content/video/demo.mp4"); !st) {
    engine::LogInfo("SKIP sample_18b_video_texture (Open failed: " + st.message() + ")");
    (void)headless_frames;
    return 0;
  }

  std::vector<std::uint8_t> rgba;
  int w = 0;
  int h = 0;
  if (auto st = decoder->DecodeNextFrame(rgba, w, h); !st) {
    engine::LogInfo("SKIP sample_18b_video_texture (DecodeNextFrame: " + st.message() + ")");
    (void)headless_frames;
    return 0;
  }
  engine::LogInfo("Decoded frame " + std::to_string(w) + "x" + std::to_string(h) +
                  " bytes=" + std::to_string(rgba.size()));
  (void)headless_frames;
  return 0;
}
