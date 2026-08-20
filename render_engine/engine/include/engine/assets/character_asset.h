#pragma once

#include "engine/assets/gltf_loader.h"
#include "engine/assets/image_loader.h"

#include <filesystem>
#include <string>
#include <vector>

namespace engine::assets {

// Mega-W11 / ADR 0038: prefer CC0 humanoid glTF under content/characters/,
// else procedural capsule mesh (same topology as BuildCapsuleCharacterMesh).
// W16 ADR 0040: skinned multi-mesh → draw_parts (multi-draw, skin preserved).
struct CharacterLoadResult {
  bool used_capsule_fallback = false;
  bool has_skin = false;
  std::filesystem::path source_path;  // glTF path when loaded; empty on capsule
  std::string note;                   // short diagnostic for samples/logs
  GltfMeshAsset mesh;                 // primary (mesh0 or static merge)
  std::vector<GltfMeshAsset> draw_parts;  // skinned extra draws; empty if single
};

struct CharacterAsset {
  // Static multi-mesh → merge all nodes/prims into mesh.
  // Skinned → mesh0 + optional draw_parts for other skinned meshes (no merge/clear skin).
  [[nodiscard]] static CharacterLoadResult TryLoadGltfOrCapsule(
      const std::filesystem::path& gltf_or_glb_path, const IImageLoader& images,
      float capsule_radius = 0.35f, float capsule_height = 1.8f);

  [[nodiscard]] static CharacterLoadResult TryLoadFromCharactersDirOrCapsule(
      const std::filesystem::path& characters_dir, const IImageLoader& images,
      float capsule_radius = 0.35f, float capsule_height = 1.8f);
};

}  // namespace engine::assets
