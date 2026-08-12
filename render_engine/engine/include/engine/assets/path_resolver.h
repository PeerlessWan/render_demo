#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets {

// M2: lightweight path convention (AssetId arrives M3).
// Reject ".." escape. Relative paths resolve against search roots.
class PathResolver {
 public:
  void AddRoot(std::filesystem::path root);
  [[nodiscard]] std::optional<std::filesystem::path> Resolve(std::string_view relative) const;

 private:
  std::vector<std::filesystem::path> roots_;
};

[[nodiscard]] bool is_safe_relative(std::string_view relative);

}  // namespace engine::assets
