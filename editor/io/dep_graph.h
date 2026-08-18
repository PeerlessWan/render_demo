#pragma once

#include "engine/assets/manifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

struct DepNode {
  std::string id;
  std::string type;
  std::string path;
  std::vector<std::string> deps;
};

struct DepGraph {
  std::vector<DepNode> nodes;
  std::string message;
  bool ok = false;
};

DepGraph BuildDepGraph(const std::filesystem::path& manifest_or_root);

[[nodiscard]] std::string DepGraphJson(const DepGraph& g);

}  // namespace editor
