#pragma once

#include "engine/core/result.h"

#include <string>
#include <string_view>

namespace engine::net {

// Probe MsQuic without bundling: LoadLibrary / known path / ENGINE_WITH_MSQUIC.
// Never installs SDKs. Sets Feature "quic" when present.
[[nodiscard]] bool ProbeMsQuicPresent();

[[nodiscard]] bool ProbeAndSetQuicFeature();

struct QuicProbeInfo {
  bool dll_or_lib_present = false;
  bool linked = false;  // ENGINE_WITH_MSQUIC compile-time
  std::string detail;
};

[[nodiscard]] QuicProbeInfo QueryMsQuicProbeInfo();

// Remote connect (product hosts). Without full session wiring → Unavailable SKIP.
[[nodiscard]] Status TryQuicConnectStub(std::string_view host, int port);

// ADR 0044: ENGINE_WITH_MSQUIC=ON + msquic.dll → real MsQuicOpenVersion path.
// OFF or missing DLL → Unavailable SKIP (no simulated Ok).
[[nodiscard]] Status TryQuicLoopbackReliableSendRecv();

}  // namespace engine::net
