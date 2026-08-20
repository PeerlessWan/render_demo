#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <filesystem>
#include <string>

namespace engine::hlod {

// C07 / Mega-W9 / W20: billboard HLOD — distance switch between near mesh and impostor.
// Single-axis billboard only (not multi-view commercial impostor). CPU bake + host upload.

enum class LodMode {
  NearMesh = 0,
  Impostor = 1,
};

struct BillboardImpostor {
  // Enter Impostor when distance >= distance_threshold (far).
  float distance_threshold = 50.f;
  // Exit back to NearMesh when distance < exit_distance (hysteresis). 0 → 0.85×threshold.
  float exit_distance = 0.f;
  std::string near_mesh_id = "mesh/near";
  std::string impostor_tex_id;

  // Stateful switch with enter/exit hysteresis (W20). Not multi-view.
  [[nodiscard]] LodMode SwitchLod(float distance);
  [[nodiscard]] LodMode current_mode() const { return current_mode_; }

 private:
  LodMode current_mode_ = LodMode::NearMesh;
};

// Placeholder "bake": returns a solid-color texture id string for unit tests (no GPU).
[[nodiscard]] std::string BakeImpostorPlaceholder(const ColorRgba& color);

// Mega-W10: write placeholder bake id + impostor fields to a small text file (CPU, no GPU).
[[nodiscard]] Status SerializeBakeToFile(const BillboardImpostor& impostor,
                                         const ColorRgba& bake_color,
                                         const std::filesystem::path& path);

// W18: write 2×2 RGBA8 solid bake next to path (path.stem + ".rgba8"); returns Ok with path message.
[[nodiscard]] Status WriteSolidRgba8Bake(const ColorRgba& color, const std::filesystem::path& path);

// W18: read SerializeBakeToFile text back into impostor (distance + ids).
[[nodiscard]] Status LoadBakeFromFile(BillboardImpostor& out, const std::filesystem::path& path);

}  // namespace engine::hlod
