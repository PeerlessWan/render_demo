#include "engine/debug/sandbox_harness.h"

#include <cctype>
#include <sstream>

namespace engine::debug {
namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

bool ExtractString(std::string_view json, std::string_view key, std::string& out) {
  const std::string pat = "\"" + std::string(key) + "\"";
  const auto pos = json.find(pat);
  if (pos == std::string_view::npos) {
    return false;
  }
  auto colon = json.find(':', pos + pat.size());
  if (colon == std::string_view::npos) {
    return false;
  }
  auto q1 = json.find('"', colon + 1);
  if (q1 == std::string_view::npos) {
    return false;
  }
  auto q2 = json.find('"', q1 + 1);
  if (q2 == std::string_view::npos) {
    return false;
  }
  out.assign(json.substr(q1 + 1, q2 - q1 - 1));
  return true;
}

bool ExtractInt(std::string_view json, std::string_view key, int& out) {
  const std::string pat = "\"" + std::string(key) + "\"";
  const auto pos = json.find(pat);
  if (pos == std::string_view::npos) {
    return false;
  }
  auto colon = json.find(':', pos + pat.size());
  if (colon == std::string_view::npos) {
    return false;
  }
  out = std::atoi(std::string(json.substr(colon + 1)).c_str());
  return true;
}

bool ExtractFloat3(std::string_view json, float& x, float& y, float& z) {
  const auto pos = json.find("\"pos\"");
  if (pos == std::string_view::npos) {
    return false;
  }
  auto lb = json.find('[', pos);
  auto rb = json.find(']', lb);
  if (lb == std::string_view::npos || rb == std::string_view::npos) {
    return false;
  }
  std::string inner(json.substr(lb + 1, rb - lb - 1));
  std::stringstream ss(inner);
  char comma = 0;
  ss >> x >> comma >> y >> comma >> z;
  return static_cast<bool>(ss);
}

}  // namespace

bool ParseHarnessLine(std::string_view line, HarnessCommand& out, std::string& error) {
  line = Trim(line);
  out = {};
  if (line.empty() || line.front() == '#') {
    error = "empty";
    return false;
  }
  if (!ExtractString(line, "cmd", out.cmd)) {
    error = "missing cmd";
    return false;
  }
  (void)ExtractString(line, "key", out.key);
  (void)ExtractString(line, "value", out.value);
  (void)ExtractString(line, "path", out.key);  // capture path alias
  (void)ExtractInt(line, "n", out.n);
  (void)ExtractFloat3(line, out.fx, out.fy, out.fz);
  return true;
}

std::string HarnessOk(std::string_view extra_json) {
  if (extra_json.empty()) {
    return "{\"ok\":true}";
  }
  return std::string("{\"ok\":true,") + std::string(extra_json) + "}";
}

std::string HarnessErr(std::string_view message) {
  return std::string("{\"ok\":false,\"error\":\"") + std::string(message) + "\"}";
}

}  // namespace engine::debug
