// asset_cook v0: validate manifest deps and write a cook stamp (M9).
#include "engine/assets/manifest.h"

#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: asset_cook <manifest.json> <out_stamp>\n";
    return 2;
  }
  auto manifest = engine::assets::Manifest::LoadFromFile(argv[1]);
  if (!manifest) {
    std::cerr << manifest.status().message() << "\n";
    return 1;
  }
  for (const auto& [id, entry] : manifest->entries()) {
    for (const auto& dep : entry.deps) {
      if (!manifest->Find(dep)) {
        std::cerr << "missing dep " << dep.value() << " for " << id.value() << "\n";
        return 1;
      }
    }
  }
  std::ofstream out(argv[2]);
  out << "cook_ok assets=" << manifest->entries().size() << "\n";
  return 0;
}
