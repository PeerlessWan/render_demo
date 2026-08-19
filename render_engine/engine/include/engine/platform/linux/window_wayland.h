#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <string>

namespace engine::platform::linux_wayland {

// W15 / ADR 0039: Wayland window + VK_KHR_wayland_surface (alongside X11 CI baseline).
struct WaylandWindowDesc {
  std::string title = "engine";
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  bool headless = false;
};

struct WaylandNative {
  void* display = nullptr;   // wl_display*
  void* surface = nullptr;   // wl_surface*
  void* egl_window = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

[[nodiscard]] Status CreateWaylandWindowStub(const WaylandWindowDesc& desc, WaylandNative& out);
[[nodiscard]] Status DestroyWaylandWindowStub(WaylandNative& native);

}  // namespace engine::platform::linux_wayland
