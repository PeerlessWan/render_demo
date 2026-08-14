#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/debug/sandbox_harness.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Write(const std::string& body) {
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

std::string ReadMessage() {
  std::string header;
  int content_length = -1;
  while (std::getline(std::cin, header)) {
    if (!header.empty() && header.back() == '\r') {
      header.pop_back();
    }
    if (header.empty()) {
      break;
    }
    if (header.rfind("Content-Length:", 0) == 0) {
      content_length = std::atoi(header.c_str() + 15);
    }
  }
  if (content_length <= 0) {
    return {};
  }
  std::string body(static_cast<std::size_t>(content_length), '\0');
  std::cin.read(body.data(), content_length);
  return body;
}

std::string EscapeJson(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

int main() {
  engine::LogInfo("sandbox_mcp ready (stdio MCP <-> harness)");
  for (;;) {
    const std::string msg = ReadMessage();
    if (msg.empty()) {
      break;
    }
    int id = 1;
    const auto id_pos = msg.find("\"id\"");
    if (id_pos != std::string::npos) {
      id = std::atoi(msg.c_str() + id_pos + 5);
    }

    if (msg.find("initialize") != std::string::npos &&
        msg.find("notifications/initialized") == std::string::npos) {
      Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
            ",\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
            "\"serverInfo\":{\"name\":\"sandbox_mcp\",\"version\":\"0.1.0\"}}}");
      continue;
    }
    if (msg.find("initialized") != std::string::npos) {
      continue;
    }
    if (msg.find("tools/list") != std::string::npos) {
      Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
            ",\"result\":{\"tools\":["
            "{\"name\":\"ping\",\"description\":\"Harness ping\",\"inputSchema\":{\"type\":\"object\"}},"
            "{\"name\":\"query_features\",\"description\":\"Query features\",\"inputSchema\":{\"type\":\"object\"}},"
            "{\"name\":\"capture\",\"description\":\"Capture hint\",\"inputSchema\":{\"type\":\"object\"}},"
            "{\"name\":\"toggle\",\"description\":\"Toggle hint\",\"inputSchema\":{\"type\":\"object\"}}"
            "]}}");
      continue;
    }
    if (msg.find("tools/call") != std::string::npos) {
      std::string text;
      if (msg.find("\"ping\"") != std::string::npos) {
        text = engine::debug::HarnessOk("\"pong\":true");
      } else if (msg.find("\"query_features\"") != std::string::npos) {
        text = engine::debug::HarnessOk(
            std::string("\"d3d12\":") + (engine::QueryFeature("d3d12") ? "true" : "false") +
            ",\"gpu_instancing\":" + (engine::QueryFeature("gpu_instancing") ? "true" : "false") +
            ",\"execute_indirect\":" +
            (engine::QueryFeature("execute_indirect") ? "true" : "false"));
      } else if (msg.find("\"capture\"") != std::string::npos) {
        text = engine::debug::HarnessOk(
            "\"hint\":\"sample_sandbox --harness-stdio or ENGINE_GOLDEN_DUMP\"");
      } else if (msg.find("\"toggle\"") != std::string::npos) {
        text = engine::debug::HarnessOk("\"hint\":\"use sandbox --harness-stdio\"");
      } else {
        text = engine::debug::HarnessErr("unknown tool");
      }
      Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
            ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"" + EscapeJson(text) +
            "\"}]}}");
      continue;
    }
    Write("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
          ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
  }
  return 0;
}
