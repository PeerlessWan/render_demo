#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_kit {

// GK-G05: poll .lua mtimes under a root (or watch list). First Poll baselines; later
// returns true when any watched file changed. Reload never touches Device.
class ScriptHotReload {
 public:
  void SetRoot(std::filesystem::path root) { root_ = std::move(root); }
  [[nodiscard]] const std::filesystem::path& root() const { return root_; }

  void WatchFile(std::filesystem::path path);
  void ClearWatches();

  // First call baselines and returns false. Later: true if any .lua mtime/set changed.
  bool Poll();

  [[nodiscard]] const std::vector<std::filesystem::path>& changed_files() const {
    return changed_;
  }

 private:
  std::filesystem::path root_;
  std::vector<std::filesystem::path> extra_;
  std::unordered_map<std::string, std::filesystem::file_time_type> mtimes_;
  std::vector<std::filesystem::path> changed_;
  bool primed_ = false;
};

}  // namespace game_kit
