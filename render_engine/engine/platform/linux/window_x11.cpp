// Mega-W11: X11 window path under __linux__ (+ ENGINE_HAS_X11). Windows keeps Unavailable stubs.
// Window::Create factory for __linux__ mirrors win32/window_win32.cpp.
// Wayland postponed — this wave requires X11. See docs/LINUX.md.

#include "engine/platform/linux/window_x11.h"
#include "engine/platform/linux/window_wayland.h"

#include "engine/core/log.h"
#include "engine/platform/window.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#if defined(__linux__) && defined(ENGINE_HAS_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
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
  const ::Window root = RootWindow(display, screen);
  const unsigned long black = BlackPixel(display, screen);
  const unsigned long white = WhitePixel(display, screen);
  ::Window window = XCreateSimpleWindow(display, root, 0, 0,
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
  XSelectInput(display, window,
               ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                   ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
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
    const ::Window window = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(native.window));
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
  // Surface helper lives in vulkan_device.cpp (TryCreateXlibSurface). Full swapchain
  // clear still goes through CreateVulkanDevice + Application.
  return Status::Fail(ErrorCode::Unavailable,
                      "TryX11ClearPathStub Unavailable SKIP: use CreateVulkanDevice "
                      "(VK_KHR_xlib_surface via TryCreateXlibSurface)");
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
                      "TryX11ClearPathStub Unavailable SKIP: ENGINE_HAS_X11 off / "
                      "VK_KHR_xlib_surface path needs X11");
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

#if defined(__linux__)

namespace engine {
namespace {

class WindowHeadless final : public Window {
 public:
  WindowHeadless(std::uint32_t width, std::uint32_t height) : width_(width), height_(height) {}

  [[nodiscard]] void* native_handle() const override { return nullptr; }
  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }
  [[nodiscard]] bool should_close() const override { return should_close_; }
  [[nodiscard]] bool is_headless() const override { return true; }
  [[nodiscard]] const WindowInputSnapshot& input_snapshot() const override { return input_; }

  void PumpEvents() override {}
  void RequestClose() override { should_close_ = true; }
  void ConsumeMouseDelta() override {
    input_.mouse_dx = 0.f;
    input_.mouse_dy = 0.f;
  }
  void ConsumeMouseWheel() override { input_.mouse_wheel = 0.f; }

 private:
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool should_close_ = false;
  WindowInputSnapshot input_{};
};

#if defined(ENGINE_HAS_X11)

// Map X11 KeySym → Win32-like VK indices consumed by Application::MapVk.
void SetKeyFromKeySym(WindowInputSnapshot& input, KeySym ks, bool down) {
  std::size_t vk = 0;
  switch (ks) {
    case XK_w:
    case XK_W:
      vk = 'W';
      break;
    case XK_a:
    case XK_A:
      vk = 'A';
      break;
    case XK_s:
    case XK_S:
      vk = 'S';
      break;
    case XK_d:
    case XK_D:
      vk = 'D';
      break;
    case XK_q:
    case XK_Q:
      vk = 'Q';
      break;
    case XK_e:
    case XK_E:
      vk = 'E';
      break;
    case XK_space:
      vk = 0x20;  // VK_SPACE
      break;
    case XK_Escape:
      vk = 0x1B;  // VK_ESCAPE
      break;
    case XK_Shift_L:
    case XK_Shift_R:
      vk = 0x10;  // VK_SHIFT
      break;
    default:
      return;
  }
  if (vk < input.keys.size()) {
    input.keys[vk] = down;
  }
}

class WindowX11 final : public Window {
 public:
  WindowX11(std::uint32_t width, std::uint32_t height) {
    native_.width = width;
    native_.height = height;
  }

  ~WindowX11() override {
    auto st = platform::linux_x11::DestroyX11WindowStub(native_);
    (void)st;
  }

  [[nodiscard]] bool Attach(platform::linux_x11::X11Native native) {
    native_ = native;
    return native_.display != nullptr && native_.window != nullptr;
  }

  // DeviceDesc.native_window points at X11Native (display + Window XID).
  [[nodiscard]] void* native_handle() const override {
    return const_cast<platform::linux_x11::X11Native*>(&native_);
  }
  [[nodiscard]] std::uint32_t width() const override { return native_.width; }
  [[nodiscard]] std::uint32_t height() const override { return native_.height; }
  [[nodiscard]] bool should_close() const override { return should_close_; }
  [[nodiscard]] const WindowInputSnapshot& input_snapshot() const override { return input_; }

  void PumpEvents() override {
    input_.text.clear();
    if (!native_.display) {
      return;
    }
    Display* dpy = static_cast<Display*>(native_.display);
    while (XPending(dpy) > 0) {
      XEvent ev{};
      XNextEvent(dpy, &ev);
      switch (ev.type) {
        case ClientMessage:
          should_close_ = true;
          break;
        case DestroyNotify:
          should_close_ = true;
          break;
        case ConfigureNotify:
          if (ev.xconfigure.width > 0 && ev.xconfigure.height > 0) {
            native_.width = static_cast<std::uint32_t>(ev.xconfigure.width);
            native_.height = static_cast<std::uint32_t>(ev.xconfigure.height);
          }
          break;
        case KeyPress:
        case KeyRelease: {
          const KeySym ks = XLookupKeysym(&ev.xkey, 0);
          SetKeyFromKeySym(input_, ks, ev.type == KeyPress);
          break;
        }
        case ButtonPress:
        case ButtonRelease: {
          const bool down = ev.type == ButtonPress;
          if (ev.xbutton.button == Button1) {
            input_.mouse_left = down;
          } else if (ev.xbutton.button == Button3) {
            input_.mouse_right = down;
          } else if (ev.xbutton.button == Button2) {
            input_.mouse_middle = down;
          } else if (ev.xbutton.button == Button4 && down) {
            input_.mouse_wheel += 1.f;
          } else if (ev.xbutton.button == Button5 && down) {
            input_.mouse_wheel -= 1.f;
          }
          if (down) {
            input_.mouse_dx = 0.f;
            input_.mouse_dy = 0.f;
          }
          break;
        }
        case MotionNotify: {
          const int x = ev.xmotion.x;
          const int y = ev.xmotion.y;
          if (have_mouse_) {
            input_.mouse_dx += static_cast<float>(x - last_mouse_x_);
            input_.mouse_dy += static_cast<float>(y - last_mouse_y_);
          }
          last_mouse_x_ = x;
          last_mouse_y_ = y;
          input_.mouse_x = static_cast<float>(x);
          input_.mouse_y = static_cast<float>(y);
          have_mouse_ = true;
          break;
        }
        default:
          break;
      }
    }
  }

  void RequestClose() override { should_close_ = true; }

  void ConsumeMouseDelta() override {
    input_.mouse_dx = 0.f;
    input_.mouse_dy = 0.f;
  }

  void ConsumeMouseWheel() override { input_.mouse_wheel = 0.f; }

 private:
  platform::linux_x11::X11Native native_{};
  bool should_close_ = false;
  WindowInputSnapshot input_{};
  bool have_mouse_ = false;
  int last_mouse_x_ = 0;
  int last_mouse_y_ = 0;
};

#endif  // ENGINE_HAS_X11

}  // namespace

Result<std::unique_ptr<Window>> Window::Create(const WindowDesc& desc) {
  if (desc.width == 0 || desc.height == 0) {
    return Result<std::unique_ptr<Window>>::Fail("Window size must be non-zero");
  }

  if (desc.headless) {
    LogInfo("Headless window created (linux)");
    return Result<std::unique_ptr<Window>>::Ok(
        std::unique_ptr<Window>(std::make_unique<WindowHeadless>(desc.width, desc.height)));
  }

  // W15: prefer Wayland when WAYLAND_DISPLAY is set and ENGINE_HAS_WAYLAND; else X11.
#if defined(ENGINE_HAS_WAYLAND)
  {
    const char* wd = std::getenv("WAYLAND_DISPLAY");
    const char* prefer_x11 = std::getenv("ENGINE_FORCE_X11");
    if (wd && wd[0] && !(prefer_x11 && prefer_x11[0] == '1')) {
      platform::linux_wayland::WaylandWindowDesc wdesc;
      wdesc.title = desc.title;
      wdesc.width = desc.width;
      wdesc.height = desc.height;
      platform::linux_wayland::WaylandNative native;
      const auto wst = platform::linux_wayland::CreateWaylandWindowStub(wdesc, native);
      if (wst) {
        LogInfo("Wayland window probe Ok — falling through to X11 mapped window for "
                "present until xdg-shell surface is wired (W15)");
        (void)platform::linux_wayland::DestroyWaylandWindowStub(native);
      } else {
        LogWarn(std::string("Wayland probe: ") + wst.message() + "; using X11");
      }
    }
  }
#endif

#if defined(ENGINE_HAS_X11)
  platform::linux_x11::X11WindowDesc xdesc;
  xdesc.title = desc.title;
  xdesc.width = desc.width;
  xdesc.height = desc.height;
  xdesc.headless = false;

  platform::linux_x11::X11Native native;
  const auto st = platform::linux_x11::CreateX11WindowStub(xdesc, native);
  if (!st) {
    return Result<std::unique_ptr<Window>>::Fail(st);
  }

  auto window = std::make_unique<WindowX11>(desc.width, desc.height);
  if (!window->Attach(native)) {
    (void)platform::linux_x11::DestroyX11WindowStub(native);
    return Result<std::unique_ptr<Window>>::Fail("WindowX11 Attach failed");
  }

  (void)desc.hidden;
  LogInfo(desc.hidden ? "X11 window created (hidden request ignored; mapped for surface)"
                      : "X11 window created");
  return Result<std::unique_ptr<Window>>::Ok(std::unique_ptr<Window>(std::move(window)));
#else
  return Result<std::unique_ptr<Window>>::Fail(
      Status::Fail(ErrorCode::Unavailable,
                   "Window::Create Unavailable: ENGINE_HAS_X11 off (install libx11-dev)"));
#endif
}

}  // namespace engine

#endif  // __linux__
