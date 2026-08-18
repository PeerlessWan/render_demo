#include "sim/sfx.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace mc {
namespace {

engine::media::AudioClip MakeTone(float freq, float seconds, float amp) {
  engine::media::AudioClip clip;
  clip.sample_rate = 22050;
  clip.channels = 1;
  const int n = static_cast<int>(clip.sample_rate * seconds);
  clip.samples.resize(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(clip.sample_rate);
    const float env = 1.f - t / seconds;
    clip.samples[static_cast<std::size_t>(i)] = amp * env * std::sin(6.2831853f * freq * t);
  }
  return clip;
}

void WriteWav(const std::filesystem::path& path, const engine::media::AudioClip& clip) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return;
  }
  const int n = static_cast<int>(clip.samples.size());
  const std::uint32_t data_bytes = static_cast<std::uint32_t>(n * 2);
  const std::uint32_t rate = static_cast<std::uint32_t>(clip.sample_rate);
  const std::uint32_t br = rate * 2;
  out.write("RIFF", 4);
  const std::uint32_t riff = 36 + data_bytes;
  out.write(reinterpret_cast<const char*>(&riff), 4);
  out.write("WAVEfmt ", 8);
  const std::uint32_t fmt = 16;
  out.write(reinterpret_cast<const char*>(&fmt), 4);
  const std::uint16_t pcm = 1, ch = 1, bits = 16, align = 2;
  out.write(reinterpret_cast<const char*>(&pcm), 2);
  out.write(reinterpret_cast<const char*>(&ch), 2);
  out.write(reinterpret_cast<const char*>(&rate), 4);
  out.write(reinterpret_cast<const char*>(&br), 4);
  out.write(reinterpret_cast<const char*>(&align), 2);
  out.write(reinterpret_cast<const char*>(&bits), 2);
  out.write("data", 4);
  out.write(reinterpret_cast<const char*>(&data_bytes), 4);
  for (float s : clip.samples) {
    float v = s;
    if (v > 1.f) {
      v = 1.f;
    }
    if (v < -1.f) {
      v = -1.f;
    }
    const std::int16_t i16 = static_cast<std::int16_t>(v * 32767.f);
    out.write(reinterpret_cast<const char*>(&i16), 2);
  }
}

}  // namespace

void InitSfx(Sfx* sfx) {
  if (!sfx) {
    return;
  }
  sfx->device = engine::media::CreateDefaultAudioDevice();
  sfx->click = MakeTone(880.f, 0.06f, 0.35f);
  sfx->hurt = MakeTone(180.f, 0.12f, 0.45f);
  const auto dir = std::filesystem::path("sfx");
  WriteWav(dir / "click.wav", sfx->click);
  WriteWav(dir / "hurt.wav", sfx->hurt);
  sfx->ready = true;
}

void PlayClick(Sfx* sfx) {
  if (!sfx || !sfx->ready) {
    return;
  }
  if (sfx->device) {
    (void)sfx->device->Play(sfx->click);
  }
  (void)engine::media::PlayWavFile(std::filesystem::path("sfx") / "click.wav");
}

void PlayHurt(Sfx* sfx) {
  if (!sfx || !sfx->ready) {
    return;
  }
  if (sfx->device) {
    (void)sfx->device->Play(sfx->hurt);
  }
  (void)engine::media::PlayWavFile(std::filesystem::path("sfx") / "hurt.wav");
}

}  // namespace mc
