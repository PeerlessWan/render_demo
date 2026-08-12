#include "engine/assets/path_resolver.h"

#include <algorithm>

namespace engine::assets {

bool is_safe_relative(std::string_view relative) {
  if (relative.empty()) {
    return false;
  }
  if (relative.starts_with('/') || relative.starts_with('\\')) {
    return false;
  }
  // Drive letter: "C:..."
  if (relative.size() >= 2 && relative[1] == ':') {
    return false;
  }
  std::filesystem::path p(relative);
  if (p.is_absolute()) {
    return false;
  }
  for (const auto& part : p) {
    if (part == "..") {
      return false;
    }
  }
  return true;
}

void PathResolver::AddRoot(std::filesystem::path root) { roots_.push_back(std::move(root)); }

std::optional<std::filesystem::path> PathResolver::Resolve(std::string_view relative) const {
  if (!is_safe_relative(relative)) {
    return std::nullopt;
  }
  const std::filesystem::path rel(relative);
  for (const auto& root : roots_) {
    auto candidate = root / rel;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec) {
      return candidate.lexically_normal();
    }
  }
  return std::nullopt;
}

}  // namespace engine::assets
