#include "engine/core/log.h"
#include "engine/media/media.h"

#include <cmath>
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

  // Synthetic PCM clip (no assets required). Product path: LoadWavPcm16 + PlayWavFile.
  engine::media::AudioClip clip;
  clip.sample_rate = 44100;
  clip.channels = 1;
  clip.samples.assign(4410, 0.f);  // 0.1s silence
  for (std::size_t i = 0; i < clip.samples.size(); ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(clip.sample_rate);
    clip.samples[i] = 0.2f * std::sin(2.f * 3.14159265f * 440.f * t);
  }
  engine::LogInfo("AudioClip samples=" + std::to_string(clip.samples.size()) +
                  " rate=" + std::to_string(clip.sample_rate));

  auto null_dev = engine::media::CreateNullAudioDevice();
  if (!null_dev) {
    engine::LogError("CreateNullAudioDevice failed");
    return 1;
  }
  engine::LogInfo(std::string("NullAudio backend=") + null_dev->backend_name());
  if (auto st = null_dev->Play(clip, 0.5f); !st) {
    engine::LogError(st.message());
    return 1;
  }
  null_dev->StopAll();

  auto out = engine::media::CreateDefaultAudioDevice();
  if (!out) {
    engine::LogInfo("SKIP sample_18c_audio_playback (CreateDefaultAudioDevice null)");
    (void)headless_frames;
    return 0;
  }
  engine::LogInfo(std::string("DefaultAudio backend=") + out->backend_name());
  // Default device may require a real .wav path on Windows PlaySound; synthetic clip is Ok on null.
  if (auto st = out->Play(clip, 0.25f); !st) {
    engine::LogInfo("SKIP sample_18c_audio_playback (Play: " + st.message() +
                    "; contract exercised via NullAudio)");
    (void)headless_frames;
    return 0;
  }
  out->StopAll();
  (void)headless_frames;
  return 0;
}
