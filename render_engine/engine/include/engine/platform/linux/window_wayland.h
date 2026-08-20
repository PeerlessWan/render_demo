#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <string>

namespace engine::platform {

// Discriminator for DeviceDesc.native_window on Linux (X11 vs Wayland).
enum class LinuxNativeKind : std::uint32_t { X11 = 1, Wayland = 2 };

}  // namespace engine::platform

namespace engine::platform::linux_wayland {

struct WaylandWindowDesc {
  std::string title = "engine";
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  bool headless = false;
};

// W16 ADR 0040: display + wl_surface + xdg-shell for VK_KHR_wayland_surface.
struct WaylandNative {
  LinuxNativeKind kind = LinuxNativeKind::Wayland;
  void* display = nullptr;      // wl_display*
  void* surface = nullptr;      // wl_surface*
  void* compositor = nullptr;   // wl_compositor*
  void* registry = nullptr;     // wl_registry*
  void* xdg_wm_base = nullptr;  // xdg_wm_base*
  void* xdg_surface = nullptr;  // xdg_surface*
  void* xdg_toplevel = nullptr; // xdg_toplevel*
  void* egl_window = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

[[nodiscard]] Status CreateWaylandWindowStub(const WaylandWindowDesc& desc, WaylandNative& out);
[[nodiscard]] Status DestroyWaylandWindowStub(WaylandNative& native);

}  // namespace engine::platform::linux_wayland
