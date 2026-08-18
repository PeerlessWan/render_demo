// Mega-W10: X11 window path under __linux__ (+ ENGINE_HAS_X11). Windows keeps Unavailable stubs.
// Wayland postponed — this wave requires X11. See docs/LINUX.md.

#include "engine/platform/linux/window_x11.h"

#include <cstdint>

#if defined(__linux__) && defined(ENGINE_HAS_X11)
#include <X11/Xlib.h>
#include <cstring>
#endif

namespace engine::platform::linux_x11 {

volatile int g_window_x11_stub_anchor = 0;

#if defined(__linux__) && defined(ENGINE_HAS_X11)

Status CreateX11WindowStub(const X11WindowDesc& desc, X11Native& out) {
  out = {};
  out.width = desc.width;
  out.height = desc.height;
  if (desc.headless) {
    return Status::Ok("x11-headless-stub");
  }

  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    return Status::Fail(ErrorCode::Unavailable,
                        "CreateX11WindowStub Unavailable: XOpenDisplay failed "
                        "(no DISPLAY / X authority; try xvfb-run)");
  }

  const int screen = DefaultScreen(display);
  const Window root = RootWindow(display, screen);
  const unsigned long black = BlackPixel(display, screen);
  const unsigned long white = WhitePixel(display, screen);
  Window window = XCreateSimpleWindow(display, root, 0, 0,
                                      desc.width > 0 ? desc.width : 1,
                                      desc.height > 0 ? desc.height : 1,
                                      1, black, white);
  if (window == 0) {
    XCloseDisplay(display);
    return Status::Fail(ErrorCode::Failed, "CreateX11WindowStub: XCreateSimpleWindow failed");
  }

  if (!desc.title.empty()) {
    XStoreName(display, window, desc.title.c_str());
  }
  XSelectInput(display, window, ExposureMask | StructureNotifyMask | KeyPressMask);
  XMapWindow(display, window);
  XFlush(display);

  out.display = display;
  out.window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(window));
  return Status::Ok("x11-window");
}

Status DestroyX11WindowStub(X11Native& native) {
  if (!native.display) {
    native = {};
    return Status::Ok("x11-destroy-noop");
  }
  Display* display = static_cast<Display*>(native.display);
  if (native.window) {
    const Window window = static_cast<Window>(reinterpret_cast<std::uintptr_t>(native.window));
    XDestroyWindow(display, window);
  }
  XCloseDisplay(display);
  native = {};
  return Status::Ok("x11-destroy");
}

Status TryX11ClearPathStub(const X11Native& native) {
  if (!native.display || !native.window) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryX11ClearPathStub Unavailable SKIP: no X11 native "
                        "(need CreateX11WindowStub non-headless)");
  }
  return Status::Fail(ErrorCode::Unavailable,
                      "TryX11ClearPathStub Unavailable SKIP: VK_KHR_xlib_surface not wired");
}

#elif defined(__linux__)

Status CreateX11WindowStub(const X11WindowDesc& desc, X11Native& out) {
  out = {};
  out.width = desc.width;
  out.height = desc.height;
  if (desc.headless) {
    return Status::Ok("x11-headless-stub");
  }
  return Status::Fail(ErrorCode::Unavailable,
                      "CreateX11WindowStub Unavailable: ENGINE_HAS_X11 off "
                      "(install libx11-dev; see docs/LINUX.md)");
}

Status DestroyX11WindowStub(X11Native& native) {
  native = {};
  return Status::Ok("x11-destroy-noop");
}

Status TryX11ClearPathStub(const X11Native&) {
  return Status::Fail(ErrorCode::Unavailable,
                      "TryX11ClearPathStub Unavailable SKIP: VK_KHR_xlib_surface not wired");
}

#else

Status CreateX11WindowStub(const X11WindowDesc&, X11Native& out) {
  out = {};
  return Status::Fail(ErrorCode::Unavailable,
                      "CreateX11WindowStub Unavailable: not __linux__ (Windows build)");
}

Status DestroyX11WindowStub(X11Native& native) {
  native = {};
  return Status::Fail(ErrorCode::Unavailable,
                      "DestroyX11WindowStub Unavailable: not __linux__");
}

Status TryX11ClearPathStub(const X11Native&) {
  return Status::Fail(ErrorCode::Unavailable,
                      "TryX11ClearPathStub Unavailable: not __linux__");
}

#endif

}  // namespace engine::platform::linux_x11
