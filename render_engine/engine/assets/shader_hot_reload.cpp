#include "engine/assets/shader_hot_reload.h"

#include <system_error>

namespace engine::assets {
namespace {

bool IsShaderFile(const std::filesystem::path& p) {
  const auto ext = p.extension().string();
  return ext == ".hlsl" || ext == ".cso" || ext == ".HLSL" || ext == ".CSO";
}

}  // namespace

bool ShaderHotReload::Poll() {
  if (dir_.empty() || !std::filesystem::exists(dir_)) {
    return false;
  }
  std::unordered_map<std::string, std::filesystem::file_time_type> next;
  bool changed = false;
  std::error_code ec;
  const auto opts = std::filesystem::directory_options::skip_permission_denied;
  for (std::filesystem::recursive_directory_iterator it(dir_, opts, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const auto& path = it->path();
    if (!IsShaderFile(path)) {
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
    needs_pso_rebuild_ = true;
  }
  return changed;
}

}  // namespace engine::assets
