#include "engine/core/log.h"
#include "engine/net/net_system.h"
#include "engine/net/quic.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

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

  engine::net::NetSystem net;
  bool http_done = false;
  (void)net.http().Get("http://127.0.0.1:9/learn_loopback",
                       [&](engine::Status st, engine::net::HttpResponse resp) {
                         http_done = true;
                         engine::LogInfo(std::string("HTTP callback ok=") + (st ? "true" : "false") +
                                         " status=" + std::to_string(resp.status_code) +
                                         (resp.error.empty() ? "" : (" err=" + resp.error)));
                       });

  for (int i = 0; i < 40 && !http_done; ++i) {
    net.Pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  net.Pump();
  engine::LogInfo(std::string("Net.Pump drained http_done=") + (http_done ? "true" : "false"));

  const bool msquic = engine::net::ProbeAndSetQuicFeature();
  const auto info = engine::net::QueryMsQuicProbeInfo();
  engine::LogInfo(std::string("MsQuic present=") + (msquic ? "true" : "false") +
                  " detail=" + info.detail);
  if (auto st = engine::net::TryQuicConnectStub("127.0.0.1", 4433); !st) {
    engine::LogInfo("QuicConnectStub: " + st.message() + " (missing MsQuic → SKIP is expected)");
  } else {
    engine::LogInfo("QuicConnectStub Ok");
  }

  (void)headless_frames;
  return 0;
}
