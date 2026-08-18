#include "engine/ui/retained_ui.h"

#include <algorithm>
#include <cmath>

namespace engine::ui {
namespace {

bool Contains(const Widget& w, float px, float py) {
  return px >= w.x && px <= w.x + w.w && py >= w.y && py <= w.y + w.h;
}

}  // namespace

void RetainedUi::Clear() {
  widgets_.clear();
  dragging_slider_.clear();
}

Widget* RetainedUi::Find(std::string_view id) {
  for (auto& w : widgets_) {
    if (w.id == id) {
      return &w;
    }
  }
  return nullptr;
}

const Widget* RetainedUi::Find(std::string_view id) const {
  for (const auto& w : widgets_) {
    if (w.id == id) {
      return &w;
    }
  }
  return nullptr;
}

void RetainedUi::Panel(std::string_view id, float x, float y, float w, float h, ColorRgba color) {
  Widget widget;
  widget.id = std::string(id);
  widget.x = x;
  widget.y = y;
  widget.w = w;
  widget.h = h;
  widget.kind = WidgetKind::Panel;
  widget.color = color;
  widgets_.push_back(std::move(widget));
}

void RetainedUi::Label(std::string_view id, std::string_view text, float x, float y) {
  Widget w;
  w.id = std::string(id);
  w.text = std::string(text);
  w.x = x;
  w.y = y;
  w.w = 280.f;
  w.h = 18.f;
  w.kind = WidgetKind::Label;
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
  w.kind = WidgetKind::Button;
  w.color = {0.25f, 0.45f, 0.75f, 0.95f};
  widgets_.push_back(std::move(w));
}

void RetainedUi::Toggle(std::string_view id, std::string_view text, float x, float y, float w,
                        float h, bool value) {
  Widget widget;
  widget.id = std::string(id);
  widget.text = std::string(text);
  widget.x = x;
  widget.y = y;
  widget.w = w;
  widget.h = h;
  widget.kind = WidgetKind::Toggle;
  widget.bool_value = value;
  widget.color = {0.2f, 0.35f, 0.28f, 0.95f};
  widgets_.push_back(std::move(widget));
}

void RetainedUi::Slider(std::string_view id, std::string_view text, float x, float y, float w,
                        float h, float value, float min_v, float max_v) {
  Widget widget;
  widget.id = std::string(id);
  widget.text = std::string(text);
  widget.x = x;
  widget.y = y;
  widget.w = w;
  widget.h = h;
  widget.kind = WidgetKind::Slider;
  widget.min_value = min_v;
  widget.max_value = max_v;
  widget.float_value = std::clamp(value, min_v, max_v);
  widget.color = {0.18f, 0.2f, 0.26f, 0.95f};
  widgets_.push_back(std::move(widget));
}

void RetainedUi::set_visible(std::string_view id, bool visible) {
  if (auto* w = Find(id)) {
    w->visible = visible;
  }
}

void RetainedUi::set_text(std::string_view id, std::string_view text) {
  if (auto* w = Find(id)) {
    w->text = std::string(text);
  }
}

void RetainedUi::set_bool(std::string_view id, bool value) {
  if (auto* w = Find(id)) {
    w->bool_value = value;
  }
}

void RetainedUi::set_float(std::string_view id, float value) {
  if (auto* w = Find(id)) {
    w->float_value = std::clamp(value, w->min_value, w->max_value);
  }
}

bool RetainedUi::get_bool(std::string_view id, bool fallback) const {
  if (const auto* w = Find(id)) {
    return w->bool_value;
  }
  return fallback;
}

float RetainedUi::get_float(std::string_view id, float fallback) const {
  if (const auto* w = Find(id)) {
    return w->float_value;
  }
  return fallback;
}

std::optional<std::string> RetainedUi::HitTest(float px, float py) const {
  for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
    if (!it->visible || it->kind == WidgetKind::Label) {
      continue;
    }
    if (Contains(*it, px, py)) {
      return it->id;
    }
  }
  return std::nullopt;
}

std::vector<UiEvent> RetainedUi::Pump(float mouse_x, float mouse_y, bool mouse_down,
                                      bool mouse_pressed) {
  std::vector<UiEvent> events;
  want_capture_ = false;

  if (const auto hit = HitTest(mouse_x, mouse_y)) {
    want_capture_ = true;
  }
  if (!dragging_slider_.empty()) {
    want_capture_ = true;
  }

  if (!mouse_down) {
    dragging_slider_.clear();
  }

  if (!dragging_slider_.empty()) {
    if (auto* w = Find(dragging_slider_)) {
      const float t =
          w->w > 1.f ? std::clamp((mouse_x - w->x) / w->w, 0.f, 1.f) : 0.f;
      const float v = w->min_value + t * (w->max_value - w->min_value);
      if (std::fabs(v - w->float_value) > 1e-5f) {
        w->float_value = v;
        UiEvent e;
        e.id = w->id;
        e.type = UiEventType::SliderChanged;
        e.float_value = v;
        events.push_back(std::move(e));
      }
    }
  }

  if (mouse_pressed) {
    if (const auto hit = HitTest(mouse_x, mouse_y)) {
      if (auto* w = Find(*hit)) {
        if (w->kind == WidgetKind::Button) {
          UiEvent e;
          e.id = w->id;
          e.type = UiEventType::Click;
          events.push_back(std::move(e));
        } else if (w->kind == WidgetKind::Toggle) {
          w->bool_value = !w->bool_value;
          UiEvent e;
          e.id = w->id;
          e.type = UiEventType::Toggle;
          e.bool_value = w->bool_value;
          events.push_back(std::move(e));
        } else if (w->kind == WidgetKind::Slider) {
          dragging_slider_ = w->id;
          const float t =
              w->w > 1.f ? std::clamp((mouse_x - w->x) / w->w, 0.f, 1.f) : 0.f;
          w->float_value = w->min_value + t * (w->max_value - w->min_value);
          UiEvent e;
          e.id = w->id;
          e.type = UiEventType::SliderChanged;
          e.float_value = w->float_value;
          events.push_back(std::move(e));
        } else if (w->kind == WidgetKind::Panel) {
          want_capture_ = true;
        }
      }
    }
  }

  return events;
}

std::vector<UiDrawRect> RetainedUi::BuildDrawList() const {
  std::vector<UiDrawRect> rects;
  for (const auto& w : widgets_) {
    if (!w.visible) {
      continue;
    }
    if (w.kind == WidgetKind::Label) {
      // Text glyphs not available yet — draw a thin accent bar as label marker.
      UiDrawRect bar;
      bar.x0 = w.x;
      bar.y0 = w.y + 4.f;
      bar.x1 = w.x + 4.f;
      bar.y1 = w.y + w.h - 2.f;
      bar.color = {0.55f, 0.75f, 1.f, 0.9f};
      rects.push_back(bar);
      continue;
    }
    if (w.kind == WidgetKind::Panel) {
      UiDrawRect bg{w.x, w.y, w.x + w.w, w.y + w.h, w.color};
      rects.push_back(bg);
      continue;
    }
    if (w.kind == WidgetKind::Button) {
      rects.push_back({w.x, w.y, w.x + w.w, w.y + w.h, w.color});
      continue;
    }
    if (w.kind == WidgetKind::Toggle) {
      ColorRgba c = w.bool_value ? ColorRgba{0.25f, 0.7f, 0.4f, 0.95f}
                                 : ColorRgba{0.35f, 0.35f, 0.38f, 0.95f};
      rects.push_back({w.x, w.y, w.x + w.w, w.y + w.h, c});
      // Knob
      const float knob_w = 18.f;
      const float kx0 = w.bool_value ? w.x + w.w - knob_w - 3.f : w.x + 3.f;
      rects.push_back({kx0, w.y + 3.f, kx0 + knob_w, w.y + w.h - 3.f, {0.92f, 0.92f, 0.94f, 1.f}});
      continue;
    }
    if (w.kind == WidgetKind::Slider) {
      rects.push_back({w.x, w.y, w.x + w.w, w.y + w.h, w.color});
      const float span = std::max(w.max_value - w.min_value, 1e-5f);
      const float t = std::clamp((w.float_value - w.min_value) / span, 0.f, 1.f);
      rects.push_back(
          {w.x, w.y, w.x + w.w * t, w.y + w.h, {0.35f, 0.65f, 0.95f, 0.95f}});
      const float hx = w.x + w.w * t;
      rects.push_back({hx - 3.f, w.y - 2.f, hx + 3.f, w.y + w.h + 2.f, {0.95f, 0.95f, 1.f, 1.f}});
    }
  }
  return rects;
}

void RetainedUi::LayoutColumn(std::string_view parent, float gap) {
  Widget* p = Find(parent);
  if (!p) {
    return;
  }
  float y = p->y + 8.f;
  const float x = p->x + 8.f;
  bool after = false;
  for (auto& w : widgets_) {
    if (w.id == p->id) {
      after = true;
      continue;
    }
    if (!after) {
      continue;
    }
    if (w.kind == WidgetKind::Panel) {
      break;
    }
    w.x = x;
    w.y = y;
    y += w.h + gap;
  }
  p->h = std::max(p->h, y - p->y + 8.f);
}

}  // namespace engine::ui
