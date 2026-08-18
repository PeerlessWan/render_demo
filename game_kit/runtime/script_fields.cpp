#include "game_kit/script_fields.h"

#include <cctype>
#include <sstream>

namespace game_kit {
namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

}  // namespace

std::vector<ScriptField> ParseScriptExports(std::string_view source) {
  std::vector<ScriptField> out;
  std::size_t i = 0;
  while (i < source.size()) {
    std::size_t eol = source.find('\n', i);
    if (eol == std::string_view::npos) {
      eol = source.size();
    }
    auto line = Trim(source.substr(i, eol - i));
    i = eol + 1;
    constexpr std::string_view kTag = "--@export ";
    if (line.size() < kTag.size() || line.substr(0, kTag.size()) != kTag) {
      continue;
    }
    auto rest = Trim(line.substr(kTag.size()));
    ScriptField f;
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos) {
      f.name = std::string(rest);
      f.type = "string";
    } else {
      f.name = std::string(Trim(rest.substr(0, colon)));
      auto ty = Trim(rest.substr(colon + 1));
      const auto eq = ty.find('=');
      if (eq == std::string_view::npos) {
        f.type = std::string(ty);
      } else {
        f.type = std::string(Trim(ty.substr(0, eq)));
        f.value = std::string(Trim(ty.substr(eq + 1)));
      }
    }
    if (!f.name.empty()) {
      out.push_back(std::move(f));
    }
  }
  return out;
}

std::string FieldsToPersist(const std::vector<ScriptField>& fields) {
  std::ostringstream o;
  for (const auto& f : fields) {
    o << f.name << '=' << f.value << '\n';
  }
  return o.str();
}

void ApplyPersistLine(std::vector<ScriptField>* fields, std::string_view line) {
  if (!fields) {
    return;
  }
  const auto eq = line.find('=');
  if (eq == std::string_view::npos) {
    return;
  }
  const auto name = std::string(Trim(line.substr(0, eq)));
  const auto value = std::string(Trim(line.substr(eq + 1)));
  for (auto& f : *fields) {
    if (f.name == name) {
      f.value = value;
      return;
    }
  }
  ScriptField f;
  f.name = name;
  f.type = "string";
  f.value = value;
  fields->push_back(std::move(f));
}

void OverlayPersistBlob(std::vector<ScriptField>* fields, std::string_view blob) {
  if (!fields || blob.empty()) {
    return;
  }
  std::size_t i = 0;
  while (i < blob.size()) {
    std::size_t end = blob.find_first_of("\n|", i);
    if (end == std::string_view::npos) {
      end = blob.size();
    }
    ApplyPersistLine(fields, blob.substr(i, end - i));
    i = end + 1;
  }
}

void OverlayJsonObject(std::vector<ScriptField>* fields, std::string_view json) {
  if (!fields || json.empty()) {
    return;
  }
  std::size_t i = 0;
  while (i < json.size()) {
    const auto q = json.find('"', i);
    if (q == std::string_view::npos) {
      break;
    }
    const auto q2 = json.find('"', q + 1);
    if (q2 == std::string_view::npos) {
      break;
    }
    const auto colon = json.find(':', q2 + 1);
    if (colon == std::string_view::npos) {
      break;
    }
    const auto name = std::string(json.substr(q + 1, q2 - q - 1));
    std::size_t vs = colon + 1;
    while (vs < json.size() && std::isspace(static_cast<unsigned char>(json[vs]))) {
      ++vs;
    }
    std::string value;
    if (vs < json.size() && json[vs] == '"') {
      const auto e = json.find('"', vs + 1);
      if (e == std::string_view::npos) {
        break;
      }
      value = std::string(json.substr(vs + 1, e - vs - 1));
      i = e + 1;
    } else {
      std::size_t ve = vs;
      while (ve < json.size() && json[ve] != ',' && json[ve] != '}' &&
             !std::isspace(static_cast<unsigned char>(json[ve]))) {
        ++ve;
      }
      value = std::string(Trim(json.substr(vs, ve - vs)));
      i = ve;
    }
    ApplyPersistLine(fields, name + "=" + value);
  }
}

std::string MergeExportsAndPersist(std::string_view source, std::string_view persist) {
  auto fields = ParseScriptExports(source);
  auto blob = Trim(persist);
  if (!blob.empty() && blob.front() == '{') {
    OverlayJsonObject(&fields, blob);
  } else {
    OverlayPersistBlob(&fields, persist);
  }
  if (fields.empty()) {
    return std::string(persist);
  }
  return FieldsToPersist(fields);
}

}  // namespace game_kit
