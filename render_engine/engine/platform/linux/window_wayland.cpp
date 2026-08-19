// W15 / ADR 0039: Wayland window path under __linux__ + ENGINE_HAS_WAYLAND.
// X11 remains CI baseline. See docs/LINUX.md.

#include "engine/platform/linux/window_wayland.h"

#include "engine/core/log.h"

#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__) && defined(ENGINE_HAS_WAYLAND)
#include <wayland-client.h>
#endif

namespace engine::platform::linux_wayland {

Status CreateWaylandWindowStub(const WaylandWindowDesc& desc, WaylandNative& out) {
  out = {};
  out.width = desc.width;
  out.height = desc.height;
  if (desc.headless) {
    return Status::Ok("wayland-headless-stub");
  }

#if defined(__linux__) && defined(ENGINE_HAS_WAYLAND)
  const char* wd = std::getenv("WAYLAND_DISPLAY");
  if (!wd || !wd[0]) {
    return Status::Fail(ErrorCode::Unavailable,
                        "CreateWaylandWindowStub Unavailable: WAYLAND_DISPLAY unset "
                        "(fall back to X11 or set Wayland session)");
  }
  wl_display* display = wl_display_connect(nullptr);
  if (!display) {
    return Status::Fail(ErrorCode::Unavailable,
                        "CreateWaylandWindowStub Unavailable: wl_display_connect failed");
  }
  // Minimal connect smoke — full xdg-shell surface wiring is follow-on.
  // Keep display so Vulkan can attach VK_KHR_wayland_surface in a later pass.
  out.display = display;
  LogInfo(std::string("Wayland display connected (") + wd +
          "); surface/xdg-shell product path pending");
  return Status::Ok("wayland-display-connected");
#else
  (void)desc;
  return Status::Fail(ErrorCode::Unavailable,
                      "CreateWaylandWindowStub Unavailable: ENGINE_HAS_WAYLAND off "
                      "(install libwayland-dev) or not __linux__");
#endif
}

Status DestroyWaylandWindowStub(WaylandNative& native) {
#if defined(__linux__) && defined(ENGINE_HAS_WAYLAND)
  if (native.display) {
    wl_display_disconnect(static_cast<wl_display*>(native.display));
    native.display = nullptr;
  }
#else
  (void)native;
#endif
  native = {};
  return Status::Ok();
}

}  // namespace engine::platform::linux_wayland
