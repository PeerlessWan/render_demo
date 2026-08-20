// Minimal xdg-shell interface descriptors (W16).

#include "engine/platform/linux/xdg_shell_minimal.h"

#include <stddef.h>

static const struct wl_interface* xdg_wm_base_types[] = {
    NULL, &xdg_surface_interface, NULL, &wl_surface_interface};

static const struct wl_message xdg_wm_base_requests[] = {
    {"destroy", "", xdg_wm_base_types},
    {"create_positioner", "n", xdg_wm_base_types},
    {"get_xdg_surface", "no", xdg_wm_base_types + 1},
    {"pong", "u", xdg_wm_base_types},
};

static const struct wl_message xdg_wm_base_events[] = {
    {"ping", "u", xdg_wm_base_types},
};

const struct wl_interface xdg_wm_base_interface = {
    "xdg_wm_base",
    1,
    4,
    xdg_wm_base_requests,
    1,
    xdg_wm_base_events,
};

static const struct wl_interface* xdg_surface_types[] = {NULL, &xdg_toplevel_interface};

static const struct wl_message xdg_surface_requests[] = {
    {"destroy", "", xdg_surface_types},
    {"get_toplevel", "n", xdg_surface_types},
    {"get_popup", "n?oo", xdg_surface_types},
    {"set_window_geometry", "iiii", xdg_surface_types},
    {"ack_configure", "u", xdg_surface_types},
};

static const struct wl_message xdg_surface_events[] = {
    {"configure", "u", xdg_surface_types},
};

const struct wl_interface xdg_surface_interface = {
    "xdg_surface",
    1,
    5,
    xdg_surface_requests,
    1,
    xdg_surface_events,
};

static const struct wl_message xdg_toplevel_requests[] = {
    {"destroy", "", NULL},
    {"set_parent", "?o", NULL},
    {"set_title", "s", NULL},
    {"set_app_id", "s", NULL},
};

static const struct wl_message xdg_toplevel_events[] = {
    {"configure", "iia", NULL},
    {"close", "", NULL},
};

const struct wl_interface xdg_toplevel_interface = {
    "xdg_toplevel",
    1,
    4,
    xdg_toplevel_requests,
    2,
    xdg_toplevel_events,
};
