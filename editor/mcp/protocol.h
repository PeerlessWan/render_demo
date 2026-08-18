#pragma once

#include "cmd/session.h"

#include <optional>
#include <string>
#include <string_view>

namespace editor {

struct McpRequest {
  std::string id;
  bool has_id = false;
  std::string method;
  std::string tool;
  std::string arguments;
};

[[nodiscard]] std::string JsonGetString(std::string_view json, std::string_view key);
[[nodiscard]] bool JsonGetBool(std::string_view json, std::string_view key, bool* out);
[[nodiscard]] bool JsonGetNumber(std::string_view json, std::string_view key, float* out);

bool ParseMcpRequest(std::string_view line, McpRequest* out);

[[nodiscard]] std::string HandleMcpSession(EditorSession sess, std::string_view line);
[[nodiscard]] std::string HandleMcpLine(EditorHost& host, std::string_view line);

bool EditorOpFromTool(std::string_view tool, std::string_view arguments, EditorOp* out);

}  // namespace editor
