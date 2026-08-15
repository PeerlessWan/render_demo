#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/platform/window.h"
#include "engine/rhi/i_device.h"

#include <filesystem>
#include <memory>
#include <string_view>

namespace engine::ui {

struct ImmediateUiDesc {
  std::filesystem::path ui_vs;
  std::filesystem::path ui_ps;
  // Optional CJK-capable font (e.g. Windows msyh.ttc). Empty = try system defaults then ImGui default.
  std::filesystem::path ui_font;
  float ui_font_size = 18.f;
};

// Facade over Dear ImGui (implementation may be stubbed if ENGINE_WITH_IMGUI=0).
// Sandbox/business code must use this API — never include imgui.h directly.
class ImmediateUi {
 public:
  ImmediateUi();
  ~ImmediateUi();

  ImmediateUi(const ImmediateUi&) = delete;
  ImmediateUi& operator=(const ImmediateUi&) = delete;

  [[nodiscard]] bool available() const;

  Status Init(rhi::IDevice& device, const ImmediateUiDesc& desc);
  void BeginFrame(const WindowInputSnapshot& input, float display_w, float display_h,
                  float delta_time);
  // Call after building widgets so WantCapture reflects this frame's hover (e.g. wheel).
  void RefreshCapture();
  Status Render(rhi::IDevice& device);

  [[nodiscard]] bool want_capture_mouse() const;
  [[nodiscard]] bool want_capture_keyboard() const;

  bool BeginWindow(std::string_view title, float x, float y, float w, float h);
  void EndWindow();
  void Text(std::string_view text);
  void Separator();
  bool Checkbox(std::string_view label, bool* value);
  bool SliderFloat(std::string_view label, float* value, float min_v, float max_v);
  bool SliderInt(std::string_view label, int* value, int min_v, int max_v);
  bool Button(std::string_view label, float w = 0.f, float h = 0.f);
  // Combo over null-terminated UTF-8 items; returns true when selection changes.
  bool Combo(std::string_view label, int* current, const char* const* items, int item_count);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace engine::ui
