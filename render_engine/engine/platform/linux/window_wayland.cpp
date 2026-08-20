// W16 / ADR 0040: Wayland window with compositor + xdg-shell + wl_surface.

#include "engine/platform/linux/window_wayland.h"

#include "engine/core/log.h"

#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__) && defined(ENGINE_HAS_WAYLAND)
#include "engine/platform/linux/xdg_shell_minimal.h"

#include <wayland-client.h>
#endif

namespace engine::platform::linux_wayland {

#if defined(__linux__) && defined(ENGINE_HAS_WAYLAND)
namespace {

struct RegistryState {
  wl_compositor* compositor = nullptr;
  xdg_wm_base* xdg_wm = nullptr;
};

void RegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* iface,
                    uint32_t version) {
  auto* st = static_cast<RegistryState*>(data);
  (void)version;
  if (std::strcmp(iface, "wl_compositor") == 0) {
    st->compositor =
        static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
  } else if (std::strcmp(iface, "xdg_wm_base") == 0) {
    st->xdg_wm =
        static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
  }
}

void RegistryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {RegistryGlobal, RegistryGlobalRemove};

void XdgWmPing(void*, xdg_wm_base* wm, uint32_t serial) {
  wl_proxy_marshal(reinterpret_cast<wl_proxy*>(wm), XDG_WM_BASE_PONG, serial);
}

const void* kXdgWmListener[] = {reinterpret_cast<const void*>(&XdgWmPing)};

struct XdgSurfaceState {
  bool configured = false;
};

void XdgSurfaceConfigure(void* data, xdg_surface* surface, uint32_t serial) {
  auto* st = static_cast<XdgSurfaceState*>(data);
  st->configured = true;
  xdg_surface_ack_configure(surface, serial);
}

const void* kXdgSurfaceListener[] = {reinterpret_cast<const void*>(&XdgSurfaceConfigure)};

}  // namespace
#endif

Status CreateWaylandWindowStub(const WaylandWindowDesc& desc, WaylandNative& out) {
  out = {};
  out.kind = LinuxNativeKind::Wayland;
  out.width = desc.width;
  out.height = desc.height;
  if (desc.headless) {
    return Status::Ok("wayland-headless-stub");
  }

#if defined(__linux__) && defined(ENGINE_HAS_WAYLAND)
  const char* wd = std::getenv("WAYLAND_DISPLAY");
  if (!wd || !wd[0]) {
    return Status::Fail(ErrorCode::Unavailable,
                        "CreateWaylandWindowStub Unavailable: WAYLAND_DISPLAY unset");
  }
  wl_display* display = wl_display_connect(nullptr);
  if (!display) {
    return Status::Fail(ErrorCode::Unavailable,
                        "CreateWaylandWindowStub Unavailable: wl_display_connect failed");
  }

  RegistryState reg{};
  wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &kRegistryListener, &reg);
  wl_display_roundtrip(display);

  if (!reg.compositor || !reg.xdg_wm) {
    if (registry) {
      wl_registry_destroy(registry);
    }
    wl_display_disconnect(display);
    return Status::Fail(ErrorCode::Unavailable,
                        "CreateWaylandWindowStub Unavailable: missing wl_compositor or "
                        "xdg_wm_base (ADR 0040)");
  }

  wl_proxy_add_listener(reinterpret_cast<wl_proxy*>(reg.xdg_wm),
                        reinterpret_cast<void (**)(void)>(kXdgWmListener), nullptr);

  wl_surface* surface = wl_compositor_create_surface(reg.compositor);
  if (!surface) {
    xdg_wm_base_destroy(reg.xdg_wm);
    wl_compositor_destroy(reg.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return Status::Fail(ErrorCode::Failed, "wl_compositor_create_surface failed");
  }

  xdg_surface* xdg_surf = xdg_wm_base_get_xdg_surface(reg.xdg_wm, surface);
  XdgSurfaceState xdg_st{};
  wl_proxy_add_listener(reinterpret_cast<wl_proxy*>(xdg_surf),
                        reinterpret_cast<void (**)(void)>(kXdgSurfaceListener), &xdg_st);
  xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdg_surf);
  if (!desc.title.empty()) {
    xdg_toplevel_set_title(toplevel, desc.title.c_str());
  }
  wl_surface_commit(surface);
  wl_display_roundtrip(display);

  out.display = display;
  out.surface = surface;
  out.xdg_wm_base = reg.xdg_wm;
  out.xdg_surface = xdg_surf;
  out.xdg_toplevel = toplevel;
  out.compositor = reg.compositor;
  out.registry = registry;
  LogInfo(std::string("Wayland xdg-shell surface Ok (") + wd + ")");
  return Status::Ok("wayland-xdg-surface");
#else
  (void)desc;
  return Status::Fail(ErrorCode::Unavailable,
                      "CreateWaylandWindowStub Unavailable: ENGINE_HAS_WAYLAND off "
                      "(install libwayland-dev) or not __linux__");
#endif
}

Status DestroyWaylandWindowStub(WaylandNative& native) {
#if defined(__linux__) && defined(ENGINE_HAS_WAYLAND)
  if (native.xdg_toplevel) {
    xdg_toplevel_destroy(static_cast<xdg_toplevel*>(native.xdg_toplevel));
  }
  if (native.xdg_surface) {
    xdg_surface_destroy(static_cast<xdg_surface*>(native.xdg_surface));
  }
  if (native.surface) {
    wl_surface_destroy(static_cast<wl_surface*>(native.surface));
  }
  if (native.xdg_wm_base) {
    xdg_wm_base_destroy(static_cast<xdg_wm_base*>(native.xdg_wm_base));
  }
  if (native.compositor) {
    wl_compositor_destroy(static_cast<wl_compositor*>(native.compositor));
  }
  if (native.registry) {
    wl_registry_destroy(static_cast<wl_registry*>(native.registry));
  }
  if (native.display) {
    wl_display_disconnect(static_cast<wl_display*>(native.display));
  }
#else
  (void)native;
#endif
  native = {};
  return Status::Ok();
}

}  // namespace engine::platform::linux_wayland
