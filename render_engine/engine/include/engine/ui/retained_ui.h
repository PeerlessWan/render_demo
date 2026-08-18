#pragma once

#include "engine/core/math.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

enum class WidgetKind { Label, Button, Toggle, Slider, Panel };

struct Widget {
  std::string id;
  std::string text;
  float x = 0, y = 0, w = 100, h = 24;
  bool visible = true;
  WidgetKind kind = WidgetKind::Label;
  bool bool_value = false;
  float float_value = 0.f;
  float min_value = 0.f;
  float max_value = 1.f;
  ColorRgba color{0.2f, 0.22f, 0.28f, 0.92f};
};

struct UiDrawRect {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  ColorRgba color{1, 1, 1, 1};
};

enum class UiEventType { Click, Toggle, SliderChanged };

struct UiEvent {
  std::string id;
  UiEventType type = UiEventType::Click;
  bool bool_value = false;
  float float_value = 0.f;
};

// Thin retained UI: layout + hit + draw rects (no ImGui; render via ScreenQuad).
class RetainedUi {
 public:
  virtual ~RetainedUi() = default;

  void Clear();
  void Panel(std::string_view id, float x, float y, float w, float h,
             ColorRgba color = {0.08f, 0.09f, 0.12f, 0.88f});
  void Label(std::string_view id, std::string_view text, float x, float y);
  void Button(std::string_view id, std::string_view text, float x, float y, float w, float h);
  void Toggle(std::string_view id, std::string_view text, float x, float y, float w, float h,
              bool value);
  void Slider(std::string_view id, std::string_view text, float x, float y, float w, float h,
              float value, float min_v, float max_v);

  void set_visible(std::string_view id, bool visible);
  void set_text(std::string_view id, std::string_view text);
  void set_bool(std::string_view id, bool value);
  void set_float(std::string_view id, float value);
  [[nodiscard]] bool get_bool(std::string_view id, bool fallback = false) const;
  [[nodiscard]] float get_float(std::string_view id, float fallback = 0.f) const;

  [[nodiscard]] const std::vector<Widget>& widgets() const { return widgets_; }

  void set_want_capture(bool v) { want_capture_ = v; }
  [[nodiscard]] bool want_capture() const { return want_capture_; }

  [[nodiscard]] std::optional<std::string> HitTest(float px, float py) const;

  // mouse_pressed = rising edge of left button this frame.
  std::vector<UiEvent> Pump(float mouse_x, float mouse_y, bool mouse_down, bool mouse_pressed);

  [[nodiscard]] std::vector<UiDrawRect> BuildDrawList() const;
  void LayoutColumn(std::string_view parent, float gap = 8.f);

  // RmlUi thin document hooks (default no-op).
  virtual bool LoadRmlDocument(std::string_view /*rml*/) { return false; }
  [[nodiscard]] virtual bool HasRmlDocument() const { return false; }

 private:
  Widget* Find(std::string_view id);
  const Widget* Find(std::string_view id) const;

  std::vector<Widget> widgets_;
  bool want_capture_ = false;
  std::string dragging_slider_;
};

}  // namespace engine::ui
