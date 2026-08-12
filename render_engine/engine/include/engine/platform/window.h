#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <memory>
#include <string>

namespace engine {

struct WindowDesc {
  std::string title = "render_engine";
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
};

class Window {
 public:
  virtual ~Window() = default;

  [[nodiscard]] virtual void* native_handle() const = 0;
  [[nodiscard]] virtual std::uint32_t width() const = 0;
  [[nodiscard]] virtual std::uint32_t height() const = 0;
  [[nodiscard]] virtual bool should_close() const = 0;

  // Pump OS messages; updates should_close when the user closes the window.
  virtual void PumpEvents() = 0;

  static Result<std::unique_ptr<Window>> Create(const WindowDesc& desc);
};

}  // namespace engine
