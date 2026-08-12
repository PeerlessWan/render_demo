#pragma once

#include "engine/assets/asset_id.h"
#include "engine/core/result.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets {

struct ManifestEntry {
  AssetId id;
  std::string type;
  std::string path;  // relative to a search root
  std::vector<AssetId> deps;
};

class Manifest {
 public:
  Status Add(ManifestEntry entry);
  [[nodiscard]] const ManifestEntry* Find(const AssetId& id) const;
  [[nodiscard]] const std::unordered_map<AssetId, ManifestEntry, AssetIdHash>& entries() const {
    return entries_;
  }

  // Minimal JSON subset: {"assets":[{"id":"...","type":"...","path":"...","deps":["..."]}]}
  static Result<Manifest> LoadFromFile(const std::filesystem::path& path);

 private:
  std::unordered_map<AssetId, ManifestEntry, AssetIdHash> entries_;
};

}  // namespace engine::assets
