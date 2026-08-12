#include "engine/ui/retained_ui.h"

namespace engine::ui {

void RetainedUi::Clear() { widgets_.clear(); }

void RetainedUi::Label(std::string_view id, std::string_view text, float x, float y) {
  Widget w;
  w.id = std::string(id);
  w.text = std::string(text);
  w.x = x;
  w.y = y;
  w.is_button = false;
  widgets_.push_back(std::move(w));
}

void RetainedUi::Button(std::string_view id, std::string_view text, float x, float y, float ww,
                        float h) {
  Widget w;
  w.id = std::string(id);
  w.text = std::string(text);
  w.x = x;
  w.y = y;
  w.w = ww;
  w.h = h;
  w.is_button = true;
  widgets_.push_back(std::move(w));
}

void RetainedUi::set_visible(std::string_view id, bool visible) {
  for (auto& w : widgets_) {
    if (w.id == id) {
      w.visible = visible;
    }
  }
}

}  // namespace engine::ui
