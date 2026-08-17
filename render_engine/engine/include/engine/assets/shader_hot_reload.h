#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace engine::assets {

// M26/C16: mtime poll for .hlsl / .cso under a shader directory.
// W6: Poll sets a PSO rebuild request the host consumes (Sandbox may rebuild PSO).
class ShaderHotReload {
 public:
  void SetShaderDir(std::filesystem::path dir) { dir_ = std::move(dir); }
  [[nodiscard]] const std::filesystem::path& shader_dir() const { return dir_; }

  // First call baselines mtimes and returns false.
  // Later calls return true if any tracked .hlsl/.cso changed (then refresh baseline).
  bool Poll();

  // True after a change Poll until ConsumePsoRebuildRequest clears it.
  [[nodiscard]] bool NeedsPsoRebuild() const { return needs_pso_rebuild_; }
  bool ConsumePsoRebuildRequest() {
    const bool v = needs_pso_rebuild_;
    needs_pso_rebuild_ = false;
    return v;
  }

 private:
  std::filesystem::path dir_;
  std::unordered_map<std::string, std::filesystem::file_time_type> mtimes_;
  bool primed_ = false;
  bool needs_pso_rebuild_ = false;
};

}  // namespace engine::assets
