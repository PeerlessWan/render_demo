#pragma once

// Minimal xdg-shell v1 client stubs for W16 Wayland present (ADR 0040).
// Full wayland-scanner output not vendored; only interfaces we bind/marshal.

#include <stdint.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

#define XDG_WM_BASE_DESTROY 0
#define XDG_WM_BASE_CREATE_POSITIONER 1
#define XDG_WM_BASE_GET_XDG_SURFACE 2
#define XDG_WM_BASE_PONG 3

#define XDG_SURFACE_DESTROY 0
#define XDG_SURFACE_GET_TOPLEVEL 1
#define XDG_SURFACE_GET_POPUP 2
#define XDG_SURFACE_SET_WINDOW_GEOMETRY 3
#define XDG_SURFACE_ACK_CONFIGURE 4

#define XDG_TOPLEVEL_DESTROY 0
#define XDG_TOPLEVEL_SET_PARENT 1
#define XDG_TOPLEVEL_SET_TITLE 2
#define XDG_TOPLEVEL_SET_APP_ID 3

extern const struct wl_interface xdg_wm_base_interface;
extern const struct wl_interface xdg_surface_interface;
extern const struct wl_interface xdg_toplevel_interface;

static inline struct xdg_surface*
xdg_wm_base_get_xdg_surface(struct xdg_wm_base* wm, struct wl_surface* surface) {
  struct wl_proxy* id = wl_proxy_marshal_constructor(
      (struct wl_proxy*)wm, XDG_WM_BASE_GET_XDG_SURFACE, &xdg_surface_interface, nullptr, surface);
  return (struct xdg_surface*)id;
}

static inline void xdg_wm_base_destroy(struct xdg_wm_base* wm) {
  wl_proxy_marshal((struct wl_proxy*)wm, XDG_WM_BASE_DESTROY);
  wl_proxy_destroy((struct wl_proxy*)wm);
}

static inline struct xdg_toplevel* xdg_surface_get_toplevel(struct xdg_surface* surface) {
  struct wl_proxy* id =
      wl_proxy_marshal_constructor((struct wl_proxy*)surface, XDG_SURFACE_GET_TOPLEVEL,
                                   &xdg_toplevel_interface, nullptr);
  return (struct xdg_toplevel*)id;
}

static inline void xdg_surface_ack_configure(struct xdg_surface* surface, uint32_t serial) {
  wl_proxy_marshal((struct wl_proxy*)surface, XDG_SURFACE_ACK_CONFIGURE, serial);
}

static inline void xdg_surface_destroy(struct xdg_surface* surface) {
  wl_proxy_marshal((struct wl_proxy*)surface, XDG_SURFACE_DESTROY);
  wl_proxy_destroy((struct wl_proxy*)surface);
}

static inline void xdg_toplevel_set_title(struct xdg_toplevel* toplevel, const char* title) {
  wl_proxy_marshal((struct wl_proxy*)toplevel, XDG_TOPLEVEL_SET_TITLE, title);
}

static inline void xdg_toplevel_destroy(struct xdg_toplevel* toplevel) {
  wl_proxy_marshal((struct wl_proxy*)toplevel, XDG_TOPLEVEL_DESTROY);
  wl_proxy_destroy((struct wl_proxy*)toplevel);
}

#ifdef __cplusplus
}
#endif
