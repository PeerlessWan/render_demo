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
  bool linked = false;  // ENGINE_WITH_MSQUIC compile-time link
  std::string detail;
};

[[nodiscard]] QuicProbeInfo QueryMsQuicProbeInfo();

// W16 ADR 0040: always Unavailable until MsQuic SendReliable API is wired (no session-stub Ok).
[[nodiscard]] Status TryQuicConnectStub(std::string_view host, int port);

// W16 ADR 0040: always Unavailable (no simulated-loopback Ok).
[[nodiscard]] Status TryQuicLoopbackReliableSendRecv();

}  // namespace engine::net
