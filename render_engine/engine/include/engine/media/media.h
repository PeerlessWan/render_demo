#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace engine::media {

struct AudioClip {
  int sample_rate = 44100;
  int channels = 1;
  std::vector<float> samples;
};

class IAudioDevice {
 public:
  virtual ~IAudioDevice() = default;
  virtual Status Play(const AudioClip& clip, float gain = 1.f) = 0;
  virtual void StopAll() = 0;
  [[nodiscard]] virtual const char* backend_name() const = 0;
};

class IVideoDecoder {
 public:
  virtual ~IVideoDecoder() = default;
  virtual Status Open(const std::string& path) = 0;
  virtual Status DecodeNextFrame(std::vector<std::uint8_t>& rgba, int& w, int& h) = 0;
  [[nodiscard]] virtual bool feature_available() const = 0;
  [[nodiscard]] virtual const char* backend_name() const = 0;
};

Result<AudioClip> LoadWavPcm16(const std::filesystem::path& path);
std::unique_ptr<IAudioDevice> CreateNullAudioDevice();
// Windows: PlaySound for short wav files; falls back to null elsewhere.
std::unique_ptr<IAudioDevice> CreateDefaultAudioDevice();
std::unique_ptr<IVideoDecoder> CreateD3D12VaDecoderOrStub();
// Fire-and-forget helper for UI/sfx.
Status PlayWavFile(const std::filesystem::path& path);

}  // namespace engine::media
