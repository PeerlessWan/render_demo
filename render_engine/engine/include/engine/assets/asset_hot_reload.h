#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace engine::assets {

// C16 / Mega-W8: mtime poll for textures/meshes under a content root.
// Host ConsumeInvalidateRequest then reloads affected assets (Sandbox logs only).
class AssetHotReload {
 public:
  void SetRoot(std::filesystem::path root) { root_ = std::move(root); }
  [[nodiscard]] const std::filesystem::path& root() const { return root_; }

  // First call baselines mtimes and returns false.
  // Later calls return true if any tracked texture/mesh file changed.
  bool Poll();

  [[nodiscard]] bool NeedsInvalidate() const { return needs_invalidate_; }
  bool ConsumeInvalidateRequest() {
    const bool v = needs_invalidate_;
    needs_invalidate_ = false;
    return v;
  }

 private:
  std::filesystem::path root_;
  std::unordered_map<std::string, std::filesystem::file_time_type> mtimes_;
  bool primed_ = false;
  bool needs_invalidate_ = false;
};

}  // namespace engine::assets
