#include "game_kit/script_hot_reload.h"

#include <system_error>

namespace game_kit {
namespace {

bool IsLuaFile(const std::filesystem::path& p) {
  const auto ext = p.extension().string();
  return ext == ".lua" || ext == ".LUA";
}

}  // namespace

void ScriptHotReload::WatchFile(std::filesystem::path path) { extra_.push_back(std::move(path)); }

void ScriptHotReload::ClearWatches() {
  extra_.clear();
  mtimes_.clear();
  changed_.clear();
  primed_ = false;
}

bool ScriptHotReload::Poll() {
  changed_.clear();
  std::unordered_map<std::string, std::filesystem::file_time_type> next;
  std::error_code ec;

  auto consider = [&](const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path, ec)) {
      ec.clear();
      return;
    }
    if (!IsLuaFile(path)) {
      return;
    }
    const auto key = path.lexically_normal().string();
    const auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
      ec.clear();
      return;
    }
    next[key] = mtime;
    if (primed_) {
      const auto prev = mtimes_.find(key);
      if (prev == mtimes_.end() || prev->second != mtime) {
        changed_.push_back(path);
      }
    }
  };

  if (!root_.empty() && std::filesystem::exists(root_, ec)) {
    const auto opts = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root_, opts, ec), end; !ec && it != end;
         it.increment(ec)) {
      if (it->is_regular_file(ec)) {
        consider(it->path());
      }
    }
  }
  for (const auto& p : extra_) {
    consider(p);
  }

  if (primed_) {
    for (const auto& [key, _] : mtimes_) {
      if (next.find(key) == next.end()) {
        changed_.push_back(std::filesystem::path(key));
      }
    }
  }

  mtimes_ = std::move(next);
  if (!primed_) {
    primed_ = true;
    changed_.clear();
    return false;
  }
  return !changed_.empty();
}

}  // namespace game_kit
