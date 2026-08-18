#include "io/dep_graph.h"

#include <sstream>

namespace editor {

DepGraph BuildDepGraph(const std::filesystem::path& manifest_or_root) {
  DepGraph g;
  std::filesystem::path path = manifest_or_root;
  if (std::filesystem::is_directory(path)) {
    path /= "manifest.json";
  }
  auto loaded = engine::assets::Manifest::LoadFromFile(path);
  if (!loaded) {
    g.message = loaded.status().message();
    return g;
  }
  g.ok = true;
  g.message = "ok";
  for (const auto& [id, entry] : loaded.value().entries()) {
    DepNode n;
    n.id = id.value();
    n.type = entry.type;
    n.path = entry.path;
    for (const auto& d : entry.deps) {
      n.deps.push_back(d.value());
    }
    g.nodes.push_back(std::move(n));
  }
  return g;
}

std::string DepGraphJson(const DepGraph& g) {
  std::ostringstream o;
  o << "{\"ok\":" << (g.ok ? "true" : "false") << ",\"message\":\"" << g.message << "\",\"assets\":[";
  for (std::size_t i = 0; i < g.nodes.size(); ++i) {
    const auto& n = g.nodes[i];
    if (i) {
      o << ',';
    }
    o << "{\"id\":\"" << n.id << "\",\"type\":\"" << n.type << "\",\"path\":\"" << n.path
      << "\",\"deps\":[";
    for (std::size_t d = 0; d < n.deps.size(); ++d) {
      if (d) {
        o << ',';
      }
      o << '"' << n.deps[d] << '"';
    }
    o << "]}";
  }
  o << "]}";
  return o.str();
}

}  // namespace editor
