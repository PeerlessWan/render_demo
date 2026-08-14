#pragma once

#include "engine/core/math.h"

#include <string>
#include <vector>

namespace engine::render2d {

struct AtlasFrame {
  std::string name;
  float u0 = 0.f;
  float v0 = 0.f;
  float u1 = 1.f;
  float v1 = 1.f;
};

// Minimal JSON atlas parser (expects top-level "frames" array with uv rects).
// Returns false on missing/malformed input without throwing.
bool LoadAtlasJson(const std::string& json_text, std::vector<AtlasFrame>& out_frames);

}  // namespace engine::render2d
