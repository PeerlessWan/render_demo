#include "io/scene_ext.h"

#include "editing/terrain_edit.h"
#include "editing/tile_edit.h"

#include <cstdlib>
#include <sstream>
#include <string>

namespace editor {
namespace {

void AppendFloatArray(std::ostringstream& out, const std::vector<float>& v) {
  out << '[';
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) {
      out << ',';
    }
    out << v[i];
  }
  out << ']';
}

void AppendIntArray(std::ostringstream& out, const std::vector<int>& v) {
  out << '[';
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) {
      out << ',';
    }
    out << v[i];
  }
  out << ']';
}

void ParseFloatArray(std::string_view json, std::string_view key, std::vector<float>* out) {
  if (!out) {
    return;
  }
  const std::string pat = std::string("\"") + std::string(key) + "\":[";
  const auto i = json.find(pat);
  if (i == std::string_view::npos) {
    return;
  }
  out->clear();
  const char* p = json.data() + i + pat.size();
  const char* end = json.data() + json.size();
  while (p < end && *p != ']') {
    char* next = nullptr;
    const float v = std::strtof(p, &next);
    if (next == p) {
      ++p;
      continue;
    }
    out->push_back(v);
    p = next;
    if (*p == ',') {
      ++p;
    }
  }
}

void ParseIntArray(std::string_view json, std::string_view key, std::vector<int>* out) {
  if (!out) {
    return;
  }
  const std::string pat = std::string("\"") + std::string(key) + "\":[";
  const auto i = json.find(pat);
  if (i == std::string_view::npos) {
    return;
  }
  out->clear();
  const char* p = json.data() + i + pat.size();
  const char* end = json.data() + json.size();
  while (p < end && *p != ']') {
    char* next = nullptr;
    const long v = std::strtol(p, &next, 10);
    if (next == p) {
      ++p;
      continue;
    }
    out->push_back(static_cast<int>(v));
    p = next;
    if (*p == ',') {
      ++p;
    }
  }
}

std::string JsonQuoted(std::string_view json, std::string_view key) {
  const std::string pat = std::string("\"") + std::string(key) + "\":\"";
  const auto i = json.find(pat);
  if (i == std::string_view::npos) {
    return {};
  }
  auto s = json.substr(i + pat.size());
  const auto e = s.find('"');
  if (e == std::string_view::npos) {
    return {};
  }
  return std::string(s.substr(0, e));
}

}  // namespace

void PackEditorExtensions(const EditorSettings& settings, game_kit::SceneDocument* doc) {
  if (!doc) {
    return;
  }
  std::ostringstream out;
  out << "{\"editor\":{\"heights\":";
  AppendFloatArray(out, settings.heights);
  out << ",\"tiles\":";
  AppendIntArray(out, settings.tiles);
  out << ",\"atlas\":\"" << settings.tile_atlas << "\",\"anim\":{\"current\":" << settings.anim.current
      << ",\"keys\":[" << settings.anim.keys[0] << ',' << settings.anim.keys[1] << ','
      << settings.anim.keys[2] << ',' << settings.anim.keys[3] << "],\"states\":[";
  for (std::size_t i = 0; i < settings.anim.states.size(); ++i) {
    if (i) {
      out << ',';
    }
    out << '"' << settings.anim.states[i] << '"';
  }
  out << "],\"transitions\":[";
  for (std::size_t i = 0; i < settings.anim.transitions.size(); ++i) {
    if (i) {
      out << ',';
    }
    out << "[\"" << settings.anim.transitions[i].first << "\",\"" << settings.anim.transitions[i].second
        << "\"]";
  }
  out << "]}}}";
  doc->extensions_json = out.str();
}

void UnpackEditorExtensions(const game_kit::SceneDocument& doc, EditorSettings* settings) {
  if (!settings || doc.extensions_json.empty()) {
    return;
  }
  ParseFloatArray(doc.extensions_json, "heights", &settings->heights);
  ParseIntArray(doc.extensions_json, "tiles", &settings->tiles);
  const auto atlas = JsonQuoted(doc.extensions_json, "atlas");
  if (!atlas.empty()) {
    settings->tile_atlas = atlas;
  }
  EnsureHeights(&settings->heights);
  EnsureTiles(&settings->tiles);
  ParseFloatArray(doc.extensions_json, "keys", nullptr);
  {
    std::vector<float> keys;
    ParseFloatArray(doc.extensions_json, "keys", &keys);
    for (std::size_t i = 0; i < keys.size() && i < 4; ++i) {
      settings->anim.keys[i] = keys[i];
    }
  }
  const auto cur = doc.extensions_json.find("\"current\":");
  if (cur != std::string::npos) {
    settings->anim.current = static_cast<int>(
        std::strtol(doc.extensions_json.c_str() + cur + 10, nullptr, 10));
  }
  std::vector<std::string> states;
  const auto st = doc.extensions_json.find("\"states\":[");
  if (st != std::string::npos) {
    auto p = doc.extensions_json.substr(st + 10);
    while (!p.empty() && p[0] != ']') {
      const auto q = p.find('"');
      if (q == std::string::npos) {
        break;
      }
      p = p.substr(q + 1);
      const auto e = p.find('"');
      if (e == std::string::npos) {
        break;
      }
      states.push_back(std::string(p.substr(0, e)));
      p = p.substr(e + 1);
    }
  }
  if (!states.empty()) {
    settings->anim.states = std::move(states);
  }
}

}  // namespace editor
