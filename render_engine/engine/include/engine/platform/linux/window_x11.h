#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <memory>
#include <string>

// Mega-W9 / ENGINE_LINUX_VK: minimal X11 window stubs for Linux Vulkan clear path.
// Windows-only trees compile this header; implementations are active under __linux__.
// See docs/LINUX.md — full Surface/swapchain still requires ENGINE_LINUX_VK host work.

namespace engine::platform::linux_x11 {

struct X11WindowDesc {
  std::string title = "render_engine";
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  bool headless = false;
};

// Opaque handle stand-in (Display* / Window on real X11).
struct X11Native {
  void* display = nullptr;  // Display*
  void* window = nullptr;   // Window (XID stored as pointer-sized)
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

#if defined(__linux__)

// Stub: allocates a descriptor without opening a real Display when headless.
// Non-headless returns Unavailable until libX11 path is wired (ENGINE_LINUX_VK).
[[nodiscard]] inline Status CreateX11WindowStub(const X11WindowDesc& desc, X11Native& out) {
  out = {};
  out.width = desc.width;
  out.height = desc.height;
  if (desc.headless) {
    return Status::Ok("x11-headless-stub");
  }
  return Status::Fail(ErrorCode::Unavailable,
                      "CreateX11WindowStub Unavailable: real X11 Create not wired "
                      "(ENGINE_LINUX_VK; see docs/LINUX.md)");
}

// Clear-color path placeholder for sample_01_clear on Linux.
[[nodiscard]] inline Status TryX11ClearPathStub(const X11Native& /*native*/) {
  return Status::Fail(ErrorCode::Unavailable,
                      "TryX11ClearPathStub Unavailable SKIP: VK_KHR_xlib_surface not wired");
}

#else

[[nodiscard]] inline Status CreateX11WindowStub(const X11WindowDesc&, X11Native& out) {
  out = {};
  return Status::Fail(ErrorCode::Unavailable,
                      "CreateX11WindowStub Unavailable: not __linux__ (Windows build)");
}

[[nodiscard]] inline Status TryX11ClearPathStub(const X11Native&) {
  return Status::Fail(ErrorCode::Unavailable,
                      "TryX11ClearPathStub Unavailable: not __linux__");
}

#endif

}  // namespace engine::platform::linux_x11
