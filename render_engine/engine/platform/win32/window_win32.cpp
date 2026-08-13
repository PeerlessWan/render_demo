#include "engine/platform/window.h"

#include "engine/core/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <memory>
#include <string>

namespace engine {
namespace {

constexpr const wchar_t* kWindowClassName = L"RenderEngineMainWindow";

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

  WindowInputSnapshot& mutable_input() { return input_; }

 private:
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool should_close_ = false;
  WindowInputSnapshot input_{};
};

class WindowWin32 final : public Window {
 public:
  WindowWin32(std::uint32_t width, std::uint32_t height) : width_(width), height_(height) {}

  ~WindowWin32() override {
    if (hwnd_) {
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
  }

  void AttachHwnd(HWND hwnd) { hwnd_ = hwnd; }

  [[nodiscard]] void* native_handle() const override { return hwnd_; }
  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }
  [[nodiscard]] bool should_close() const override { return should_close_; }
  [[nodiscard]] const WindowInputSnapshot& input_snapshot() const override { return input_; }

  void PumpEvents() override {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        should_close_ = true;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  void RequestClose() override {
    should_close_ = true;
    if (hwnd_) {
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
  }

  void ConsumeMouseDelta() override {
    input_.mouse_dx = 0.f;
    input_.mouse_dy = 0.f;
  }

  void OnDestroy() { should_close_ = true; }

  void OnSize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
  }

  void OnKey(WPARAM vk, bool down) {
    if (vk < input_.keys.size()) {
      input_.keys[static_cast<std::size_t>(vk)] = down;
    }
  }

  void OnMouseMove(int x, int y) {
    if (have_mouse_) {
      input_.mouse_dx += static_cast<float>(x - last_mouse_x_);
      input_.mouse_dy += static_cast<float>(y - last_mouse_y_);
    }
    last_mouse_x_ = x;
    last_mouse_y_ = y;
    input_.mouse_x = static_cast<float>(x);
    input_.mouse_y = static_cast<float>(y);
    have_mouse_ = true;
  }

  void OnMouseButton(int button, bool down) {
    if (button == 0) {
      input_.mouse_left = down;
    } else if (button == 1) {
      input_.mouse_right = down;
    }
  }

 private:
  HWND hwnd_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool should_close_ = false;
  WindowInputSnapshot input_{};
  int last_mouse_x_ = 0;
  int last_mouse_y_ = 0;
  bool have_mouse_ = false;
};

WindowWin32* WindowFromHwnd(HWND hwnd) {
  return reinterpret_cast<WindowWin32*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  if (auto* self = WindowFromHwnd(hwnd)) {
    switch (msg) {
      case WM_SIZE: {
        const auto width = static_cast<std::uint32_t>(LOWORD(lparam));
        const auto height = static_cast<std::uint32_t>(HIWORD(lparam));
        if (width > 0 && height > 0) {
          self->OnSize(width, height);
        }
        return 0;
      }
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
        self->OnKey(wparam, true);
        return 0;
      case WM_KEYUP:
      case WM_SYSKEYUP:
        self->OnKey(wparam, false);
        return 0;
      case WM_MOUSEMOVE:
        self->OnMouseMove(static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam)));
        return 0;
      case WM_LBUTTONDOWN:
        self->OnMouseButton(0, true);
        return 0;
      case WM_LBUTTONUP:
        self->OnMouseButton(0, false);
        return 0;
      case WM_RBUTTONDOWN:
        self->OnMouseButton(1, true);
        return 0;
      case WM_RBUTTONUP:
        self->OnMouseButton(1, false);
        return 0;
      case WM_CLOSE:
        self->OnDestroy();
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        self->OnDestroy();
        PostQuitMessage(0);
        return 0;
      default:
        break;
    }
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool EnsureWindowClass() {
  static bool registered = false;
  if (registered) {
    return true;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(32512));
  wc.lpszClassName = kWindowClassName;
  if (!RegisterClassExW(&wc)) {
    return false;
  }
  registered = true;
  return true;
}

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(count > 0 ? count - 1 : 0), L'\0');
  if (count > 1) {
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), count);
  }
  return wide;
}

}  // namespace

Result<std::unique_ptr<Window>> Window::Create(const WindowDesc& desc) {
  if (desc.width == 0 || desc.height == 0) {
    return Result<std::unique_ptr<Window>>::Fail("Window size must be non-zero");
  }

  if (desc.headless) {
    LogInfo("Headless window created");
    return Result<std::unique_ptr<Window>>::Ok(
        std::unique_ptr<Window>(std::make_unique<WindowHeadless>(desc.width, desc.height)));
  }

  if (!EnsureWindowClass()) {
    return Result<std::unique_ptr<Window>>::Fail("RegisterClassExW failed");
  }

  RECT rect{0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height)};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

  auto window = std::make_unique<WindowWin32>(desc.width, desc.height);
  const auto title = Utf8ToWide(desc.title);
  HWND hwnd =
      CreateWindowExW(0, kWindowClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                      CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                      nullptr, GetModuleHandleW(nullptr), window.get());
  if (!hwnd) {
    return Result<std::unique_ptr<Window>>::Fail("CreateWindowExW failed");
  }

  window->AttachHwnd(hwnd);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  LogInfo("Win32 window created");
  return Result<std::unique_ptr<Window>>::Ok(std::unique_ptr<Window>(std::move(window)));
}

}  // namespace engine
