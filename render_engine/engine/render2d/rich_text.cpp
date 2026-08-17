#include "engine/render2d/rich_text.h"

#include <cctype>
#include <cstdlib>

namespace engine::render2d {
namespace {

int HexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

bool ParseHexColor(std::string_view hex, ColorRgba& out) {
  if (!hex.empty() && hex.front() == '#') {
    hex.remove_prefix(1);
  }
  if (hex.size() != 6 && hex.size() != 8) {
    return false;
  }
  int vals[4] = {255, 255, 255, 255};
  const int comps = static_cast<int>(hex.size() / 2);
  for (int i = 0; i < comps; ++i) {
    const int hi = HexNibble(hex[static_cast<std::size_t>(i) * 2]);
    const int lo = HexNibble(hex[static_cast<std::size_t>(i) * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    vals[i] = (hi << 4) | lo;
  }
  out.r = vals[0] / 255.f;
  out.g = vals[1] / 255.f;
  out.b = vals[2] / 255.f;
  out.a = vals[3] / 255.f;
  return true;
}

void AppendText(std::vector<RichTextSpan>& out, std::string_view text, const ColorRgba& color) {
  if (text.empty()) {
    return;
  }
  if (!out.empty() && out.back().text != "\n" && text != "\n" &&
      out.back().color.r == color.r && out.back().color.g == color.g &&
      out.back().color.b == color.b && out.back().color.a == color.a) {
    out.back().text.append(text);
    return;
  }
  out.push_back(RichTextSpan{std::string(text), color});
}

}  // namespace

std::vector<RichTextSpan> ParseRichTextSpans(std::string_view input, const ColorRgba& default_color) {
  std::vector<RichTextSpan> out;
  ColorRgba cur = default_color;
  std::size_t i = 0;
  while (i < input.size()) {
    if (input[i] == '\n') {
      AppendText(out, "\n", cur);
      ++i;
      continue;
    }
    if (input[i] == '<' && i + 1 < input.size()) {
      const auto close = input.find('>', i + 1);
      if (close == std::string_view::npos) {
        AppendText(out, input.substr(i), cur);
        break;
      }
      const std::string_view tag = input.substr(i + 1, close - i - 1);
      // <n/> or <n>
      if (tag == "n/" || tag == "n" || tag == "/n") {
        AppendText(out, "\n", cur);
        i = close + 1;
        continue;
      }
      // </color> </c>
      if (tag == "/color" || tag == "/c") {
        cur = default_color;
        i = close + 1;
        continue;
      }
      // <color=#RRGGBB> or <c=#…>
      auto eq = tag.find('=');
      std::string_view name = tag;
      std::string_view value;
      if (eq != std::string_view::npos) {
        name = tag.substr(0, eq);
        value = tag.substr(eq + 1);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
          name.remove_suffix(1);
        }
      }
      if ((name == "color" || name == "c") && !value.empty()) {
        ColorRgba parsed = cur;
        if (ParseHexColor(value, parsed)) {
          cur = parsed;
        }
        i = close + 1;
        continue;
      }
      // Unknown tag: emit literally.
      AppendText(out, input.substr(i, close - i + 1), cur);
      i = close + 1;
      continue;
    }
    const auto next = input.find_first_of("<\n", i);
    if (next == std::string_view::npos) {
      AppendText(out, input.substr(i), cur);
      break;
    }
    AppendText(out, input.substr(i, next - i), cur);
    i = next;
  }
  return out;
}

}  // namespace engine::render2d
