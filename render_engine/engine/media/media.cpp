#include "engine/media/media.h"

#include <fstream>

namespace engine::media {
namespace {

struct WavHeader {
  char riff[4];
  std::uint32_t size;
  char wave[4];
};

class NullAudio final : public IAudioDevice {
 public:
  Status Play(const AudioClip& clip, float) override {
    last_samples_ = static_cast<int>(clip.samples.size());
    playing_ = true;
    return Status::Ok();
  }
  void StopAll() override { playing_ = false; }
  const char* backend_name() const override { return "null"; }
  [[nodiscard]] bool playing() const { return playing_; }
  [[nodiscard]] int last_samples() const { return last_samples_; }

 private:
  bool playing_ = false;
  int last_samples_ = 0;
};

class D3D12VaStub final : public IVideoDecoder {
 public:
  Status Open(const std::string& path) override {
    path_ = path;
    if (path.empty()) {
      return Status::Fail(ErrorCode::InvalidArgument, "empty video path");
    }
    // Feature probe stub: report unavailable until real D3D12VA adapter lands.
    available_ = false;
    return Status::Fail(ErrorCode::Unavailable, "D3D12VA not available on this build (diagnosed)");
  }
  Status DecodeNextFrame(std::vector<std::uint8_t>&, int&, int&) override {
    return Status::Fail(ErrorCode::Unavailable, "D3D12VA not available");
  }
  bool feature_available() const override { return available_; }
  const char* backend_name() const override { return "d3d12va_stub"; }

 private:
  std::string path_;
  bool available_ = false;
};

}  // namespace

Result<AudioClip> LoadWavPcm16(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result<AudioClip>::Fail("cannot open wav: " + path.string());
  }
  WavHeader hdr{};
  in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (std::string(hdr.riff, 4) != "RIFF" || std::string(hdr.wave, 4) != "WAVE") {
    return Result<AudioClip>::Fail("not a RIFF/WAVE file");
  }

  AudioClip clip;
  clip.sample_rate = 44100;
  clip.channels = 1;
  // Minimal parser: scan for "data" chunk.
  while (in) {
    char id[4];
    std::uint32_t sz = 0;
    in.read(id, 4);
    in.read(reinterpret_cast<char*>(&sz), 4);
    if (!in) {
      break;
    }
    if (std::string(id, 4) == "fmt " && sz >= 16) {
      std::uint16_t format = 0, channels = 0;
      std::uint32_t rate = 0;
      std::uint16_t bits = 0;
      in.read(reinterpret_cast<char*>(&format), 2);
      in.read(reinterpret_cast<char*>(&channels), 2);
      in.read(reinterpret_cast<char*>(&rate), 4);
      in.ignore(6);
      in.read(reinterpret_cast<char*>(&bits), 2);
      if (sz > 16) {
        in.ignore(static_cast<std::streamsize>(sz - 16));
      }
      if (format != 1 || bits != 16) {
        return Result<AudioClip>::Fail("only PCM16 wav supported");
      }
      clip.channels = channels;
      clip.sample_rate = static_cast<int>(rate);
    } else if (std::string(id, 4) == "data") {
      std::vector<char> raw(sz);
      in.read(raw.data(), static_cast<std::streamsize>(sz));
      const auto count = sz / 2;
      clip.samples.resize(count);
      for (std::uint32_t i = 0; i < count; ++i) {
        const auto sample = *reinterpret_cast<const std::int16_t*>(raw.data() + i * 2);
        clip.samples[i] = sample / 32768.f;
      }
      return Result<AudioClip>::Ok(std::move(clip));
    } else {
      in.ignore(static_cast<std::streamsize>(sz));
    }
  }
  return Result<AudioClip>::Fail("wav data chunk not found");
}

std::unique_ptr<IAudioDevice> CreateNullAudioDevice() { return std::make_unique<NullAudio>(); }

std::unique_ptr<IVideoDecoder> CreateD3D12VaDecoderOrStub() {
  return std::make_unique<D3D12VaStub>();
}

}  // namespace engine::media
