#include "engine/core/log.h"
#include "engine/media/upscaler.h"

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

  auto upscaler = engine::media::CreateUpscaler();
  if (!upscaler) {
    engine::LogError("CreateUpscaler returned null");
    return 1;
  }
  engine::LogInfo(std::string("Upscaler: ") + upscaler->name());

  std::vector<std::uint8_t> src(4 * 4 * 4, 0);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const std::size_t i = static_cast<std::size_t>((y * 4 + x) * 4);
      src[i + 0] = static_cast<std::uint8_t>(x * 60);
      src[i + 1] = static_cast<std::uint8_t>(y * 60);
      src[i + 2] = 180;
      src[i + 3] = 255;
    }
  }
  std::vector<std::uint8_t> dst;
  if (auto st = upscaler->Upscale(src, 4, 4, dst, 8, 8); !st) {
    engine::LogError(st.message());
    return 1;
  }
  engine::LogInfo("Upscaled 4x4 -> 8x8 bytes=" + std::to_string(dst.size()));

  int rw = 0;
  int rh = 0;
  engine::media::ResolutionScale::ComputeRenderSize(1280, 720, 0.5f, rw, rh);
  engine::LogInfo("ResolutionScale 1280x720 @0.5 -> " + std::to_string(rw) + "x" +
                  std::to_string(rh));
  engine::media::UpscaleParams jp;
  jp.jitter_x = 0.02f;
  jp.jitter_y = -0.01f;
  std::vector<std::uint8_t> dst_j;
  if (auto st = upscaler->Upscale(src, 4, 4, dst_j, 8, 8, jp); !st) {
    engine::LogError(st.message());
    return 1;
  }
  engine::LogInfo("Upscaled with EffectTuning-style jitter bytes=" + std::to_string(dst_j.size()));

  (void)headless_frames;
  return 0;
}
