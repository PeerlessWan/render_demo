#include "engine/render2d/atlas.h"

#include <cstdlib>

namespace engine::render2d {
namespace {

float FindFloatAfter(const std::string& text, std::size_t from, std::string_view key, float fallback) {
  const auto p = text.find(std::string("\"") + std::string(key) + "\"", from);
  if (p == std::string::npos) {
    return fallback;
  }
  const auto colon = text.find(':', p);
  if (colon == std::string::npos) {
    return fallback;
  }
  return static_cast<float>(std::atof(text.c_str() + colon + 1));
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

bool LoadAtlasJson(const std::string& json_text, std::vector<AtlasFrame>& out_frames) {
  out_frames.clear();
  const auto frames_key = json_text.find("\"frames\"");
  if (frames_key == std::string::npos) {
    return false;
  }
  const auto arr_start = json_text.find('[', frames_key);
  if (arr_start == std::string::npos) {
    return false;
  }
  const auto arr_end = json_text.find(']', arr_start);
  if (arr_end == std::string::npos) {
    return false;
  }

  std::size_t pos = arr_start;
  while (pos < arr_end) {
    const auto obj = json_text.find('{', pos);
    if (obj == std::string::npos || obj >= arr_end) {
      break;
    }
    const auto obj_end = json_text.find('}', obj);
    if (obj_end == std::string::npos || obj_end > arr_end) {
      return false;
    }
    AtlasFrame frame;
    frame.name = FindStringAfter(json_text, obj, "name");
    frame.u0 = FindFloatAfter(json_text, obj, "u0", 0.f);
    frame.v0 = FindFloatAfter(json_text, obj, "v0", 0.f);
    frame.u1 = FindFloatAfter(json_text, obj, "u1", 1.f);
    frame.v1 = FindFloatAfter(json_text, obj, "v1", 1.f);
    out_frames.push_back(std::move(frame));
    pos = obj_end + 1;
  }
  return !out_frames.empty();
}

}  // namespace engine::render2d
