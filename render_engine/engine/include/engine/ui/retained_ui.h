#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

// M8/M15: retained UI skeleton + ImGui WantCapture routing.
struct Widget {
  std::string id;
  std::string text;
  float x = 0, y = 0, w = 100, h = 24;
  bool visible = true;
  bool is_button = false;
};

class RetainedUi {
 public:
  void Clear();
  void Label(std::string_view id, std::string_view text, float x, float y);
  void Button(std::string_view id, std::string_view text, float x, float y, float w, float h);
  void set_visible(std::string_view id, bool visible);
  [[nodiscard]] const std::vector<Widget>& widgets() const { return widgets_; }

  void set_want_capture(bool v) { want_capture_ = v; }
  [[nodiscard]] bool want_capture() const { return want_capture_; }

 private:
  std::vector<Widget> widgets_;
  bool want_capture_ = false;
};

}  // namespace engine::ui
