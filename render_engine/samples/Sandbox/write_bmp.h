#pragma once

// Minimal RGBA8 → BMP writer + bounded async queue (Sandbox capture).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sandbox {

// 24-bit BGR BMP (bottom-up). Alpha discarded. Returns empty string on success.
inline std::string WriteRgbaBmp(const std::string& path, int w, int h,
                                const std::uint8_t* rgba) {
  if (w <= 0 || h <= 0 || !rgba) {
    return "invalid image";
  }
  const std::uint32_t row_stride =
      (static_cast<std::uint32_t>(w) * 3u + 3u) & ~3u;  // 4-byte aligned
  const std::uint32_t pixel_bytes = row_stride * static_cast<std::uint32_t>(h);
  const std::uint32_t file_size = 14u + 40u + pixel_bytes;

  std::vector<std::uint8_t> file(file_size, 0);
  // BITMAPFILEHEADER
  file[0] = 'B';
  file[1] = 'M';
  file[2] = static_cast<std::uint8_t>(file_size & 0xFF);
  file[3] = static_cast<std::uint8_t>((file_size >> 8) & 0xFF);
  file[4] = static_cast<std::uint8_t>((file_size >> 16) & 0xFF);
  file[5] = static_cast<std::uint8_t>((file_size >> 24) & 0xFF);
  file[10] = 54;  // offset to pixels
  // BITMAPINFOHEADER
  file[14] = 40;  // header size
  file[18] = static_cast<std::uint8_t>(w & 0xFF);
  file[19] = static_cast<std::uint8_t>((w >> 8) & 0xFF);
  file[20] = static_cast<std::uint8_t>((w >> 16) & 0xFF);
  file[21] = static_cast<std::uint8_t>((w >> 24) & 0xFF);
  file[22] = static_cast<std::uint8_t>(h & 0xFF);
  file[23] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
  file[24] = static_cast<std::uint8_t>((h >> 16) & 0xFF);
  file[25] = static_cast<std::uint8_t>((h >> 24) & 0xFF);
  file[26] = 1;   // planes
  file[28] = 24;  // bpp

  std::uint8_t* pixels = file.data() + 54;
  for (int y = 0; y < h; ++y) {
    // BMP is bottom-up.
    std::uint8_t* dst = pixels + static_cast<std::size_t>(h - 1 - y) * row_stride;
    const std::uint8_t* src =
        rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
    for (int x = 0; x < w; ++x) {
      dst[x * 3 + 0] = src[x * 4 + 2];  // B
      dst[x * 3 + 1] = src[x * 4 + 1];  // G
      dst[x * 3 + 2] = src[x * 4 + 0];  // R
    }
  }

  FILE* f = nullptr;
#if defined(_MSC_VER)
  if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) {
    return "fopen failed: " + path;
  }
#else
  f = std::fopen(path.c_str(), "wb");
  if (!f) {
    return "fopen failed: " + path;
  }
#endif
  const std::size_t n = std::fwrite(file.data(), 1, file.size(), f);
  std::fclose(f);
  if (n != file.size()) {
    return "fwrite incomplete: " + path;
  }
  return {};
}

class AsyncBmpWriter {
 public:
  static constexpr std::size_t kMaxQueue = 8;

  AsyncBmpWriter() { worker_ = std::thread([this] { ThreadMain(); }); }

  ~AsyncBmpWriter() { Shutdown(); }

  AsyncBmpWriter(const AsyncBmpWriter&) = delete;
  AsyncBmpWriter& operator=(const AsyncBmpWriter&) = delete;

  bool Enqueue(std::string path, int w, int h, std::vector<std::uint8_t> rgba) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (stop_ || queue_.size() >= kMaxQueue) {
        ++dropped_;
        return false;
      }
      queue_.push(Job{std::move(path), w, h, std::move(rgba)});
    }
    cv_.notify_one();
    return true;
  }

  std::size_t pending() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
  }

  std::uint64_t dropped() const { return dropped_.load(); }

  void DrainAndStop() { Shutdown(); }

 private:
  struct Job {
    std::string path;
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba;
  };

  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (stop_) {
        return;
      }
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  void ThreadMain() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) {
          if (stop_) {
            break;
          }
          continue;
        }
        job = std::move(queue_.front());
        queue_.pop();
      }
      (void)WriteRgbaBmp(job.path, job.w, job.h, job.rgba.data());
    }
  }

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::queue<Job> queue_;
  std::thread worker_;
  bool stop_ = false;
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace sandbox
