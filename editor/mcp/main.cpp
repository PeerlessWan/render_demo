#include "mcp/protocol.h"
#include "mcp/live_io.h"

#include <filesystem>
#include <iostream>
#include <string>

int main() {
  if (editor::LiveForwardStdio()) {
    return 0;
  }
  editor::EditorHost host;
  host.content.Scan(
      {std::filesystem::path("editor/content"), std::filesystem::path("game_kit/samples")});
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const auto out = editor::HandleMcpLine(host, line);
    if (!out.empty()) {
      std::cout << out << '\n';
      std::cout.flush();
    }
  }
  return 0;
}

