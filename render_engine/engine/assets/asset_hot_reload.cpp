#include "engine/assets/asset_hot_reload.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace engine::assets {
namespace {

bool IsAssetFile(const std::filesystem::path& p) {
  auto ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" ||
         ext == ".dds" || ext == ".ktx" || ext == ".ktx2" || ext == ".gltf" || ext == ".glb" ||
         ext == ".obj" || ext == ".fbx" || ext == ".mesh" || ext == ".bin";
}

}  // namespace

bool AssetHotReload::Poll() {
  if (root_.empty() || !std::filesystem::exists(root_)) {
    return false;
  }
  std::unordered_map<std::string, std::filesystem::file_time_type> next;
  bool changed = false;
  std::error_code ec;
  const auto opts = std::filesystem::directory_options::skip_permission_denied;
  for (std::filesystem::recursive_directory_iterator it(root_, opts, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const auto& path = it->path();
    if (!IsAssetFile(path)) {
      continue;
    }
    const auto key = path.lexically_normal().string();
    const auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    next[key] = mtime;
    if (primed_) {
      const auto prev = mtimes_.find(key);
      if (prev == mtimes_.end() || prev->second != mtime) {
        changed = true;
      }
    }
  }
  if (primed_) {
    for (const auto& [key, _] : mtimes_) {
      if (next.find(key) == next.end()) {
        changed = true;
        break;
      }
    }
  }
  mtimes_ = std::move(next);
  if (!primed_) {
    primed_ = true;
    return false;
  }
  if (changed) {
    needs_invalidate_ = true;
  }
  return changed;
}

}  // namespace engine::assets
