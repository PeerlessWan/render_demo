#pragma once

#include "engine/core/math.h"

#include <string>
#include <string_view>
#include <vector>

namespace engine::render2d {

// C15 / Mega-W8: minimal rich-text spans for BMFont layout (not full HTML).
// Supported tags:
//   <color=#RRGGBB>…</color>  or  <c=#RRGGBBAA>…</c>
//   <n/> or literal '\n' → span with text "\n" (layout treats as line break)
struct RichTextSpan {
  std::string text;
  ColorRgba color{1.f, 1.f, 1.f, 1.f};
};

[[nodiscard]] std::vector<RichTextSpan> ParseRichTextSpans(std::string_view input,
                                                           const ColorRgba& default_color = {
                                                               1.f, 1.f, 1.f, 1.f});

}  // namespace engine::render2d
