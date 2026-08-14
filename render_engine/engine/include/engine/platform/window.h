#pragma once

#include "engine/core/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace engine {

struct WindowDesc {
  std::string title = "render_engine";
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  bool headless = false;
  // Create HWND but do not ShowWindow (Vulkan gpu_headless needs a surface).
  bool hidden = false;
};

// Per-frame OS input snapshot (filled by PumpEvents).
struct WindowInputSnapshot {
  std::array<bool, 256> keys{};
  float mouse_x = 0.f;
  float mouse_y = 0.f;
  float mouse_dx = 0.f;
  float mouse_dy = 0.f;
  float mouse_wheel = 0.f;  // accumulated notches this frame (WHEEL_DELTA units / 120)
  bool mouse_left = false;
  bool mouse_right = false;
  bool mouse_middle = false;
  bool close_requested = false;
};

class Window {
 public:
  virtual ~Window() = default;

  [[nodiscard]] virtual void* native_handle() const = 0;
  [[nodiscard]] virtual std::uint32_t width() const = 0;
  [[nodiscard]] virtual std::uint32_t height() const = 0;
  [[nodiscard]] virtual bool should_close() const = 0;
  [[nodiscard]] virtual bool is_headless() const { return false; }
  [[nodiscard]] virtual const WindowInputSnapshot& input_snapshot() const = 0;

  // Pump OS messages; updates should_close / input_snapshot.
  virtual void PumpEvents() = 0;
  virtual void RequestClose() = 0;
  virtual void ConsumeMouseDelta() = 0;
  virtual void ConsumeMouseWheel() {}
  // Soft drag capture (SetCapture only). Prefer over full FPS-style lock for editor look.
  virtual void SetCursorCaptured(bool /*captured*/) {}
  // Optional: hide + clip cursor (FPS). Default off for drag-look UX.
  virtual void SetCursorLocked(bool /*locked*/) {}

  static Result<std::unique_ptr<Window>> Create(const WindowDesc& desc);
};

}  // namespace engine
