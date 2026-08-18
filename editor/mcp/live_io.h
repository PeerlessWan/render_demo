#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

inline constexpr const char* kLiveHost = "127.0.0.1";
inline constexpr std::uint16_t kLivePort = 17864;

class LiveServer {
 public:
  LiveServer() = default;
  ~LiveServer() { Stop(); }
  LiveServer(const LiveServer&) = delete;
  LiveServer& operator=(const LiveServer&) = delete;

  bool Start(std::uint16_t port = kLivePort);
  void Stop();
  void Poll(std::vector<std::string>* lines);
  void Reply(std::string_view line);
  [[nodiscard]] bool has_client() const { return client_ != 0; }

 private:
  std::uintptr_t listen_ = 0;
  std::uintptr_t client_ = 0;
  std::string buf_;
};

// If a live editor_app is listening, shuttle stdio JSON-RPC to it. Returns true if connected.
bool LiveForwardStdio(std::uint16_t port = kLivePort);

}  // namespace editor
