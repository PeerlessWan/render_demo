#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <filesystem>
#include <string>

namespace engine::hlod {

// C07 / Mega-W9: minimal billboard HLOD — distance switch between near mesh and impostor.
// CPU-only; no GPU bake this wave.

enum class LodMode {
  NearMesh = 0,
  Impostor = 1,
};

struct BillboardImpostor {
  float distance_threshold = 50.f;
  std::string near_mesh_id = "mesh/near";
  std::string impostor_tex_id;

  // distance < threshold → NearMesh; else Impostor.
  [[nodiscard]] LodMode SwitchLod(float distance) const;
};

// Placeholder "bake": returns a solid-color texture id string for unit tests (no GPU).
[[nodiscard]] std::string BakeImpostorPlaceholder(const ColorRgba& color);

// Mega-W10: write placeholder bake id + impostor fields to a small text file (CPU, no GPU).
[[nodiscard]] Status SerializeBakeToFile(const BillboardImpostor& impostor,
                                         const ColorRgba& bake_color,
                                         const std::filesystem::path& path);

}  // namespace engine::hlod
