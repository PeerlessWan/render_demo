#pragma once

#include <string>
#include <string_view>

namespace engine::debug {

// Minimal JSON-line harness protocol for Sandbox / MCP / CI.
// One JSON object per line. Responses are one JSON object per line.
//
// Commands: ping, quit, toggle, set_quality, camera, frame, capture,
//           query_features, profiler_snapshot
struct HarnessCommand {
  std::string cmd;
  std::string key;     // toggle name / quality / capture path
  std::string value;   // optional
  float fx = 0, fy = 0, fz = 0;
  int n = 0;
};

bool ParseHarnessLine(std::string_view line, HarnessCommand& out, std::string& error);
std::string HarnessOk(std::string_view extra_json = {});
std::string HarnessErr(std::string_view message);

}  // namespace engine::debug
