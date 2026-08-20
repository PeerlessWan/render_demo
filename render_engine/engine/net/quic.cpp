#include "engine/net/quic.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace engine::net {
namespace {

#if defined(_WIN32)
bool TryLoadMsQuicDll(std::string& detail) {
#if defined(ENGINE_MSQUIC_DLL_PATH)
  {
    const char* path = ENGINE_MSQUIC_DLL_PATH;
    if (path && path[0] != '\0') {
      HMODULE h = LoadLibraryA(path);
      if (h) {
        FreeLibrary(h);
        detail = std::string("loaded ") + path;
        return true;
      }
    }
  }
#endif
  HMODULE h = LoadLibraryA("msquic.dll");
  if (h) {
    FreeLibrary(h);
    detail = "loaded msquic.dll (PATH/CWD)";
    return true;
  }
  detail = "msquic.dll not found (ADR 0031: optional when present)";
  return false;
}
#else
bool TryLoadMsQuicDll(std::string& detail) {
  detail = "MsQuic probe not implemented on this platform";
  return false;
}
#endif

}  // namespace

QuicProbeInfo QueryMsQuicProbeInfo() {
  QuicProbeInfo info;
#if defined(ENGINE_WITH_MSQUIC) && ENGINE_WITH_MSQUIC
  info.linked = true;
  info.dll_or_lib_present = true;
  info.detail = "ENGINE_WITH_MSQUIC=1 (headers linked; SendReliable requires API wiring)";
#else
  info.linked = false;
  info.dll_or_lib_present = TryLoadMsQuicDll(info.detail);
#endif
  return info;
}

bool ProbeMsQuicPresent() {
  return QueryMsQuicProbeInfo().dll_or_lib_present;
}

bool ProbeAndSetQuicFeature() {
  const QuicProbeInfo info = QueryMsQuicProbeInfo();
  SetFeatureOverride("quic", info.dll_or_lib_present);
  if (info.dll_or_lib_present) {
    LogInfo(std::string("MsQuic Feature quic=true: ") + info.detail);
  } else {
    LogInfo(std::string("MsQuic Feature quic=false: ") + info.detail);
  }
  return info.dll_or_lib_present;
}

Status TryQuicConnectStub(std::string_view host, int port) {
  if (host.empty() || port <= 0) {
    return Status::Fail(ErrorCode::InvalidArgument, "TryQuicConnectStub: bad host/port");
  }
  // W16 ADR 0040: no session-stub Ok. DLL on PATH without API link ≠ product Connect.
  (void)ProbeMsQuicPresent();
  return Status::Fail(ErrorCode::Unavailable,
                      "QUIC Unavailable SKIP: Connect/SendReliable not wired without MsQuic API "
                      "(ADR 0031 / ADR 0040 — no session-stub)");
}

Status TryQuicLoopbackReliableSendRecv() {
  // W16 ADR 0040: delete simulated-loopback Ok; transport only when real MsQuic API is used.
  (void)QueryMsQuicProbeInfo();
  (void)QueryFeature("quic");
  return Status::Fail(
      ErrorCode::Unavailable,
      "TryQuicLoopbackReliableSendRecv Unavailable SKIP: MsQuic SendReliable API not wired "
      "(ADR 0040 — no simulated-loopback)");
}

}  // namespace engine::net
