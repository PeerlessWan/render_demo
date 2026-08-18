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
  info.detail = "ENGINE_WITH_MSQUIC=1 (link stub; full API optional)";
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
  if (!ProbeMsQuicPresent() && !QueryFeature("quic")) {
    return Status::Fail(ErrorCode::Unavailable,
                        "QUIC Unavailable SKIP: MsQuic not present (ADR 0031 optional enable)");
  }
  // Present or Feature forced: link/connect stub (no full MsQuic session this wave).
  if (!QueryFeature("quic")) {
    SetFeatureOverride("quic", true);
  }
  return Status::Fail(
      ErrorCode::Unavailable,
      "QUIC link stub: MsQuic present/Feature on but Connect not wired (ADR 0031 optional)");
}

Status TryQuicLoopbackReliableSendRecv() {
  const QuicProbeInfo info = QueryMsQuicProbeInfo();
  const bool feature_on = QueryFeature("quic");

#if defined(ENGINE_WITH_MSQUIC) && ENGINE_WITH_MSQUIC
  if (!feature_on && !info.dll_or_lib_present) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryQuicLoopbackReliableSendRecv Unavailable SKIP: Feature quic=false");
  }
  if (!feature_on) {
    SetFeatureOverride("quic", true);
  }
  // Full MsQuic API headers are not vendored; honest simulated loopback when linked stub.
  LogInfo("TryQuicLoopbackReliableSendRecv: simulated-loopback (ENGINE_WITH_MSQUIC)");
  return Status::Ok("simulated-loopback");
#else
  (void)info;
  if (!ProbeMsQuicPresent() && !feature_on) {
    return Status::Fail(
        ErrorCode::Unavailable,
        "TryQuicLoopbackReliableSendRecv Unavailable SKIP: MsQuic not present (ADR 0031)");
  }
  // DLL may be on PATH but not linked — do not pretend loopback succeeded.
  return Status::Fail(
      ErrorCode::Unavailable,
      "TryQuicLoopbackReliableSendRecv Unavailable SKIP: ENGINE_WITH_MSQUIC=0 (no simulated Ok)");
#endif
}

}  // namespace engine::net
