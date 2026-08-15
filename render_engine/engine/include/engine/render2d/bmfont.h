#pragma once

#include "engine/core/result.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::render2d {

// M21 stub: minimal BMFont-like JSON atlas (not AngelCode binary).
// Expected shape:
// {
//   "pages": ["font.png"],
//   "lineHeight": 16,
//   "glyphs": [
//     {"ch": "A", "x": 0, "y": 0, "w": 8, "h": 12, "xadv": 9}
//   ]
// }
struct BmGlyph {
  int x = 0;
  int y = 0;
  int w = 8;
  int h = 8;
  int xadvance = 8;
};

struct BmFontAtlas {
  std::vector<std::string> pages;
  int line_height = 16;
  std::unordered_map<char32_t, BmGlyph> glyphs;
};

Status LoadBmFontJson(const std::string& json_text, BmFontAtlas& out);
Status LoadBmFontJsonFile(const std::filesystem::path& path, BmFontAtlas& out);

}  // namespace engine::render2d
