#pragma once

#include "engine/core/result.h"
#include "engine/platform/linux/window_wayland.h"

#include <cstdint>
#include <string>

// Mega-W11 / ENGINE_LINUX_VK: X11 window path for Linux Vulkan clear.
// W16: Wayland present when WAYLAND_DISPLAY + xdg_wm_base; X11 remains CI baseline.

namespace engine::platform::linux_x11 {

struct X11WindowDesc {
  std::string title = "render_engine";
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  bool headless = false;
};

// On Linux, Window::native_handle() returns X11Native* for Vulkan VK_KHR_xlib_surface.
struct X11Native {
  LinuxNativeKind kind = LinuxNativeKind::X11;
  void* display = nullptr;  // Display*
  void* window = nullptr;   // Window (XID stored as pointer-sized)
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

[[nodiscard]] Status CreateX11WindowStub(const X11WindowDesc& desc, X11Native& out);
[[nodiscard]] Status DestroyX11WindowStub(X11Native& native);
[[nodiscard]] Status TryX11ClearPathStub(const X11Native& native);

}  // namespace engine::platform::linux_x11
