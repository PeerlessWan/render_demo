#include "engine/assets/manifest.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " <path-to-manifest.json | content-root>\n"
            << "  Validates manifest.json exists and prints asset dependency list.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 2;
  }
  std::filesystem::path target(argv[1]);
  std::filesystem::path manifest_path = target;
  if (std::filesystem::is_directory(target)) {
    manifest_path = target / "manifest.json";
  }
  if (!std::filesystem::exists(manifest_path)) {
    std::cerr << "error: manifest not found: " << manifest_path.string() << "\n";
    return 1;
  }
  auto loaded = engine::assets::Manifest::LoadFromFile(manifest_path);
  if (!loaded) {
    std::cerr << "error: " << loaded.status().message() << "\n";
    return 1;
  }
  const auto& manifest = loaded.value();
  std::cout << "manifest_ok " << manifest_path.string() << "\n";
  std::cout << "assets " << manifest.entries().size() << "\n";
  // Mega-W10 optional content roots (character / large_terrain) — warn only.
  const auto content_root = manifest_path.parent_path();
  for (const char* rel : {"scenes/large_terrain", "characters"}) {
    const auto p = content_root / rel;
    if (std::filesystem::exists(p)) {
      std::cout << "w10_content_ok " << rel << "\n";
    } else {
      std::cout << "w10_content_missing " << rel << " (optional)\n";
    }
  }
  for (const auto& [id, entry] : manifest.entries()) {
    std::cout << "asset " << id.value() << " type=" << entry.type << " path=" << entry.path;
    if (!entry.deps.empty()) {
      std::cout << " deps=";
      for (std::size_t i = 0; i < entry.deps.size(); ++i) {
        if (i) {
          std::cout << ',';
        }
        std::cout << entry.deps[i].value();
      }
    }
    std::cout << "\n";
  }
  return 0;
}
