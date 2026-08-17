#pragma once

#include "engine/core/result.h"

#include <string>
#include <string_view>

namespace engine::net {

// Probe MsQuic without bundling: LoadLibrary / known path / ENGINE_WITH_MSQUIC.
// Never installs SDKs. Sets Feature "quic" when present.
[[nodiscard]] bool ProbeMsQuicPresent();

// Apply Feature override from ProbeMsQuicPresent(); returns probe result.
[[nodiscard]] bool ProbeAndSetQuicFeature();

struct QuicProbeInfo {
  bool dll_or_lib_present = false;
  bool linked = false;  // ENGINE_WITH_MSQUIC compile-time link
  std::string detail;
};

[[nodiscard]] QuicProbeInfo QueryMsQuicProbeInfo();

// Attempt Connect when Feature quic is on. Missing MsQuic → Unavailable SKIP (ADR 0031).
[[nodiscard]] Status TryQuicConnectStub(std::string_view host, int port);

}  // namespace engine::net
