#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <string>

// Mega-W10 / ENGINE_LINUX_VK: X11 window path for Linux Vulkan clear.
// Windows trees compile this header; real Xlib lives in window_x11.cpp under __linux__ + ENGINE_HAS_X11.
// Wayland is explicitly postponed — X11 is required this wave. See docs/LINUX.md.

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

[[nodiscard]] Status CreateX11WindowStub(const X11WindowDesc& desc, X11Native& out);
[[nodiscard]] Status DestroyX11WindowStub(X11Native& native);
[[nodiscard]] Status TryX11ClearPathStub(const X11Native& native);

}  // namespace engine::platform::linux_x11
