#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace engine::assets {

// M26/C16: mtime poll for .hlsl / .cso under a shader directory.
// Full PSO rebuild is optional and left to the host (Sandbox may log).
class ShaderHotReload {
 public:
  void SetShaderDir(std::filesystem::path dir) { dir_ = std::move(dir); }
  [[nodiscard]] const std::filesystem::path& shader_dir() const { return dir_; }

  // First call baselines mtimes and returns false.
  // Later calls return true if any tracked .hlsl/.cso changed (then refresh baseline).
  bool Poll();

 private:
  std::filesystem::path dir_;
  std::unordered_map<std::string, std::filesystem::file_time_type> mtimes_;
  bool primed_ = false;
};

}  // namespace engine::assets
