#include "engine/render2d/bmfont.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace engine::render2d {
namespace {

int FindIntAfter(const std::string& text, std::size_t from, std::string_view key, int fallback) {
  const auto p = text.find(std::string("\"") + std::string(key) + "\"", from);
  if (p == std::string::npos) {
    return fallback;
  }
  const auto colon = text.find(':', p);
  if (colon == std::string::npos) {
    return fallback;
  }
  return std::atoi(text.c_str() + colon + 1);
}

std::string FindStringAfter(const std::string& text, std::size_t from, std::string_view key) {
  const auto p = text.find(std::string("\"") + std::string(key) + "\"", from);
  if (p == std::string::npos) {
    return {};
  }
  const auto q0 = text.find('"', p + static_cast<std::size_t>(key.size()) + 2);
  if (q0 == std::string::npos) {
    return {};
  }
  const auto q1 = text.find('"', q0 + 1);
  if (q1 == std::string::npos) {
    return {};
  }
  return text.substr(q0 + 1, q1 - q0 - 1);
}

}  // namespace

Status LoadBmFontJson(const std::string& json_text, BmFontAtlas& out) {
  out = {};
  out.line_height = FindIntAfter(json_text, 0, "lineHeight", 16);

  const auto pages_key = json_text.find("\"pages\"");
  if (pages_key != std::string::npos) {
    const auto arr_start = json_text.find('[', pages_key);
    const auto arr_end = json_text.find(']', arr_start);
    if (arr_start != std::string::npos && arr_end != std::string::npos) {
      std::size_t pos = arr_start;
      while (pos < arr_end) {
        const auto q0 = json_text.find('"', pos + 1);
        if (q0 == std::string::npos || q0 >= arr_end) {
          break;
        }
        const auto q1 = json_text.find('"', q0 + 1);
        if (q1 == std::string::npos || q1 > arr_end) {
          break;
        }
        out.pages.push_back(json_text.substr(q0 + 1, q1 - q0 - 1));
        pos = q1 + 1;
      }
    }
  }

  const auto glyphs_key = json_text.find("\"glyphs\"");
  if (glyphs_key == std::string::npos) {
    return Status::Fail("bmfont json missing glyphs");
  }
  const auto arr_start = json_text.find('[', glyphs_key);
  if (arr_start == std::string::npos) {
    return Status::Fail("bmfont glyphs array malformed");
  }
  // Find matching closing bracket by depth (glyphs may contain nested objects).
  int depth = 0;
  std::size_t arr_end = std::string::npos;
  for (std::size_t i = arr_start; i < json_text.size(); ++i) {
    if (json_text[i] == '[') {
      ++depth;
    } else if (json_text[i] == ']') {
      --depth;
      if (depth == 0) {
        arr_end = i;
        break;
      }
    }
  }
  if (arr_end == std::string::npos) {
    return Status::Fail("bmfont glyphs array unclosed");
  }

  std::size_t pos = arr_start;
  while (pos < arr_end) {
    const auto obj = json_text.find('{', pos);
    if (obj == std::string::npos || obj >= arr_end) {
      break;
    }
    const auto obj_end = json_text.find('}', obj);
    if (obj_end == std::string::npos || obj_end > arr_end) {
      return Status::Fail("bmfont glyph object malformed");
    }
    const std::string ch = FindStringAfter(json_text, obj, "ch");
    if (ch.empty()) {
      pos = obj_end + 1;
      continue;
    }
    BmGlyph g;
    g.x = FindIntAfter(json_text, obj, "x", 0);
    g.y = FindIntAfter(json_text, obj, "y", 0);
    g.w = FindIntAfter(json_text, obj, "w", 8);
    g.h = FindIntAfter(json_text, obj, "h", 8);
    g.xadvance = FindIntAfter(json_text, obj, "xadv", g.w);
    // First code unit only (ASCII / BMP stub).
    const char32_t code = static_cast<unsigned char>(ch[0]);
    out.glyphs[code] = g;
    pos = obj_end + 1;
  }

  if (out.glyphs.empty()) {
    return Status::Fail("bmfont json has no glyphs");
  }
  return Status::Ok();
}

Status LoadBmFontJsonFile(const std::filesystem::path& path, BmFontAtlas& out) {
  std::ifstream in(path);
  if (!in) {
    return Status::Fail("cannot open bmfont json: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return LoadBmFontJson(ss.str(), out);
}

}  // namespace engine::render2d
